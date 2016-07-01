/* 
 * ETL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>
#include "etl_state_machine.h"

// declare spin lock in unlocked state
static DEFINE_SPINLOCK(state_lock) ;

static etl_state_t current_state =
  { .current_state = 0, 
  	.error_flag = 0,
  	.error_count = 0,
  	// when following 3 members are all zero, it means fresh out of Hello handshake
  	.event_i_sent = 0,
  	.event_i_know = 0,
  	.event_send_next = 0,
#ifdef ETL_SPEED_CHECK
    .interval_time = {0,0},			// the last interval time between S <-> R transition
    .max_interval_time = {0,0}, 	// the max interval time
    .min_interval_time	= {0,0}  	// the min interval time
#endif
  };
static etl_state_t error_state =
  { .current_state = 0,
  	.error_flag = 0,
  	.error_count = 0 
  };

static etl_state_t return_state ;
static __u16 my_u_addr ;
static __u32 my_l_addr ;
static __u8 my_addr_valid = 0 ;

static __u16 hello_u_addr ;
static __u32 hello_l_addr ;
static __u8 hello_addr_valid = 0;

void etl_set_my_adder( __u16 u_addr, __u32 l_addr ) 
{
	spin_lock( &state_lock ) ;

	my_u_addr = u_addr ;
	my_l_addr = l_addr ;
	my_addr_valid = 1 ;
	hello_addr_valid = 0 ;

	spin_unlock( &state_lock ) ;
}

static void set_error( __u32 error_flag ) 
{
	struct timespec ts ;

	// Record the first error state, just count on 2nd error and after
	if( error_state.error_count == 0 ) {
	  	error_state.event_i_know = current_state.event_i_know ;
	  	error_state.event_i_sent = current_state.event_i_sent ;
	  	error_state.current_state = current_state.current_state ;
  		error_state.error_flag = error_flag ;
	  	memcpy( &error_state.update_time, &current_state.update_time, sizeof(struct timeval)) ;		
		ts = current_kernel_time();
		memcpy( &error_state.error_time, &ts, sizeof(struct timespec) ) ; 
	}
  	else{
  		error_state.p_error_flag |= error_flag ;
  	}
    error_state.error_count++ ;

}

static void clear_intervals(void)
{
#ifdef ETL_SPEED_CHECK
	memset( &current_state.interval_time, 0, sizeof(struct timespec)) ;
	memset( &current_state.max_interval_time, 0, sizeof(struct timespec)) ;
	memset( &current_state.min_interval_time, 0, sizeof(struct timespec)) ;
#endif
}


static void calc_intervals(void)
{
#ifdef ETL_SPEED_CHECK
	struct timespec ts ;
	if( current_state.update_time.tv_sec > 0 || current_state.update_time.tv_nsec > 0 ) {
		ts = current_kernel_time();
		current_state.interval_time.tv_sec = ts.tv_sec - current_state.update_time.tv_sec ;
		if( ts.tv_nsec > current_state.update_time.tv_nsec ) {
			current_state.interval_time.tv_nsec = ts.tv_nsec - current_state.update_time.tv_nsec ;
		}
		else {
			current_state.interval_time.tv_sec -= 1 ;
			current_state.interval_time.tv_nsec = 1000000000 + ts.tv_nsec - current_state.update_time.tv_nsec ;
		}
		if( current_state.max_interval_time.tv_sec < current_state.interval_time.tv_sec ||
		   ( current_state.max_interval_time.tv_sec == current_state.interval_time.tv_sec && 
		   	 current_state.max_interval_time.tv_nsec < current_state.interval_time.tv_nsec) )
		{
			current_state.max_interval_time.tv_sec = current_state.interval_time.tv_sec ;
			current_state.max_interval_time.tv_nsec = current_state.interval_time.tv_nsec ;
		}
		if( (current_state.min_interval_time.tv_sec == 0 && current_state.min_interval_time.tv_nsec == 0 ) ||
			current_state.min_interval_time.tv_sec > current_state.interval_time.tv_sec ||
			( current_state.min_interval_time.tv_sec == current_state.interval_time.tv_sec && 
		   	 current_state.min_interval_time.tv_nsec > current_state.interval_time.tv_nsec) )
		{
			current_state.min_interval_time.tv_sec = current_state.interval_time.tv_sec ;
			current_state.min_interval_time.tv_nsec = current_state.interval_time.tv_nsec ;
		}
	}
#endif
}

__u32 get_etl_state(void) 
{
	__u16 ret ;

	spin_lock( &state_lock ) ;

	if( error_state.error_count ) {
		ret = ETL_STATE_ERROR ;
	}
	else {
		ret = current_state.current_state ;
	}

	spin_unlock( &state_lock ) ;
	
	return ret ;

}

void etl_received(  __u16 u_saddr, __u32 l_saddr, __u16 u_daddr, __u32 l_daddr ) 
{
	struct timespec ts ;

	if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_NOP_U) {
		return ;
	}
	if( my_addr_valid == 0 ) {
			// say error here
		ETL_DEBUG( "message received without my address set" ) ;
		return ;		
	}

	spin_lock( &state_lock ) ;

	ts = current_kernel_time();

	switch( current_state.current_state ) {

		case ETL_STATE_IDLE:
		{
			// say something here as receive something on idle state
			ETL_DEBUG( "message received @ %ld sec on Idle state!", ts.tv_sec ) ;
		}
		break ;
		case ETL_STATE_HELLO:
		{
			if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U )
			{
				hello_u_addr = u_saddr ;
				hello_l_addr = l_saddr ;
				hello_addr_valid = 1 ;
			}
			else {
				// Received non hello message on Hello state
				ETL_DEBUG( "non-hello message %d received on hello state @ %ld sec", u_saddr, ts.tv_sec ) ;
			}			
		}
		break ;
		case ETL_STATE_WAIT:
		{
			if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U )
			{
				if( my_u_addr > u_saddr || (my_u_addr == u_saddr && my_l_addr > l_saddr ) ) {
					current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
					current_state.current_state = ETL_STATE_SEND ;		
					memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals() ; 
				}
				else if( my_u_addr == u_saddr && my_l_addr == l_saddr ) {
					// say error as Alan's 1990s problem again
					ETL_DEBUG( "hello message with same address received @ %ld sec", ts.tv_sec ) ;
					set_error( ETL_ERROR_SAME_ADDRESS ) ;
					current_state.current_state = ETL_STATE_IDLE ;		
					memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
				}
				else {
					current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
					current_state.current_state = ETL_STATE_RECEIVE ;			
					memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals() ; 
				}			// case we received before sending hello, which seems sequence error
			}
			else {
				// Received non hello message on Wait state
				ETL_DEBUG( "non-hello message %d received on hello state @ %ld sec", u_saddr, ts.tv_sec ) ;			
				set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
				current_state.current_state = ETL_STATE_HELLO ;		
				memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;			
			}		
		}
		break ;
		case ETL_STATE_SEND:
		{
			if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_EVENT_U || (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U
				) {
				set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
				current_state.current_state = ETL_STATE_HELLO ;
				memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
			}
		}
		break ;
		case ETL_STATE_RECEIVE:
		{
			if( u_daddr == ETL_MESSAGE_EVENT_U ) {
				if( current_state.event_i_know == 0 && current_state.event_i_sent == 0 && current_state.event_send_next == 0 )
				{
					// fresh out of Hello handshake
					if( l_daddr == 0 ) {
						current_state.event_i_know = l_daddr ;
						current_state.event_send_next = l_daddr + 1 ;
						current_state.current_state = ETL_STATE_SEND ;
						calc_intervals() ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
						current_state.current_state = ETL_STATE_HELLO ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					}

				}
				else {
					if( current_state.event_i_sent + 1 != l_daddr ) {
						set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
						current_state.current_state = ETL_STATE_HELLO ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						current_state.event_i_know = l_daddr ;
						current_state.event_send_next = l_daddr + 1 ;
						current_state.current_state = ETL_STATE_SEND ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
				}
			}
			else if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U ) {
				set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
				hello_u_addr = u_saddr ;
				hello_l_addr = l_saddr ;
				hello_addr_valid = 1 ;
				current_state.current_state = ETL_STATE_HELLO ;
				memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
			}

		}
		break ;
		default:
		{
			set_error( ETL_ERROR_UNKOWN_STATE ) ;
			current_state.current_state = ETL_STATE_IDLE ;		
			memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;			
		}
		break ;
	}
	spin_unlock( &state_lock ) ;
}

void etl_next_send( __u16 *u_addr, __u32 *l_addr ) 
{
	struct timespec ts ;

	spin_lock( &state_lock ) ;

	ts = current_kernel_time();

	switch( current_state.current_state ) {

		case ETL_STATE_IDLE:
		{
			// say something here as attempt to send something on idle state
			ETL_DEBUG( "Message requested on Idle state @ %ld sec", ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ETL_MESSAGE_NOP_U ;
		}
		break ;
		case ETL_STATE_HELLO:
		{
			*l_addr = ETL_MESSAGE_HELLO_L ;
			*u_addr = ETL_MESSAGE_HELLO_U ;
			if( hello_addr_valid ) {
				// case peer hello is already received
				if( my_addr_valid == 0 ) {
					// say error here
					ETL_DEBUG( "My address is not set on Hello message request @ %ld sec", ts.tv_sec ) ;			
				}
				else {
					if( my_u_addr > hello_u_addr || (my_u_addr == hello_u_addr && my_l_addr > hello_l_addr ) ) {
						current_state.current_state = ETL_STATE_SEND ;			
						current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
						clear_intervals() ; 
					}
					else if( my_u_addr == hello_u_addr && my_l_addr == hello_l_addr ) {
						// say error as Alan's 1990s problem again
						ETL_DEBUG( "hello message with same address received @ %ld sec", ts.tv_sec ) ;
						set_error( ETL_ERROR_SAME_ADDRESS ) ;
						current_state.current_state = ETL_STATE_IDLE ;		
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						current_state.current_state = ETL_STATE_RECEIVE ;			
						current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
						memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
						clear_intervals() ; 
					}
					hello_addr_valid = 0 ;	
				}
			}
			else {
				current_state.current_state = ETL_STATE_WAIT ;
				memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;
			}
		}
		break ;
		case ETL_STATE_WAIT:
		{
			*l_addr = 0 ;
			*u_addr = ETL_MESSAGE_NOP_U ;
		}
		break ;
		case ETL_STATE_SEND:
		{
			*u_addr = ETL_MESSAGE_EVENT_U ;
			
			current_state.event_i_sent = current_state.event_send_next ;
			current_state.event_send_next += 2 ;
			*l_addr = current_state.event_i_sent ;
			current_state.current_state = ETL_STATE_RECEIVE ;			
			calc_intervals() ;
			memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;

		}
		break ;
		case ETL_STATE_RECEIVE:
		{
			*l_addr = 0 ;
			*u_addr = ETL_MESSAGE_NOP_U ;			
		}
		break ;
		default:
		{
			*l_addr = 0 ;
			*u_addr = ETL_MESSAGE_NOP_U ;
		}
		break ;
	}
	spin_unlock( &state_lock ) ;

}

void etl_state_error( __u32 error_flag ) 
{
	spin_lock( &state_lock ) ;

 	set_error( error_flag ) ;

	spin_unlock( &state_lock ) ;
}

etl_state_t* etl_read_current_state(void) 
{
	spin_lock( &state_lock ) ;

  	memcpy( &return_state, &error_state, sizeof(current_state)) ;

	spin_unlock( &state_lock ) ;	
	return &return_state ;
}

etl_state_t* etl_read_error_state(void) 
{
	spin_lock( &state_lock ) ;
	
  	memcpy( &return_state, &error_state, sizeof(etl_state_t)) ;
  	error_state.error_count = 0 ;

	spin_unlock( &state_lock ) ;	
	return &return_state ;
}

void etl_link_up(void) 
{
	struct timespec ts ;

	spin_lock( &state_lock ) ;

	ts = current_kernel_time();

	if( current_state.current_state == ETL_STATE_IDLE ) {
		ETL_DEBUG( "Link UP !! @ %ld sec", ts.tv_sec ) ;
		current_state.current_state = ETL_STATE_HELLO ;
		memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;		
	}
	else {
		ETL_DEBUG( "Unexpected Link UP on state %d @ %ld sec", current_state.current_state, ts.tv_sec ) ;
		set_error( ETL_ERROR_UNEXPECTED_LU ) ;
		current_state.current_state = ETL_STATE_HELLO ;
		memcpy( &current_state.update_time, &ts, sizeof(struct timespec)) ;		
	}
	spin_unlock( &state_lock ) ;		
}

