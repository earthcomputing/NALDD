/* 
 * ETL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/string.h>
#include "etl_state_machine.h"


void etl_state_machine_init( etl_state_machine_t *mcn )
{
	mcn->current_state.current_state = 0 ;
  	mcn->current_state.error_flag = 0 ;
  	mcn->current_state.error_count = 0 ;
  	// when following 3 members are all zero, it means fresh out of Hello handshake
  	mcn->current_state.event_i_sent = 0;
  	mcn->current_state.event_i_know = 0;
  	mcn->current_state.event_send_next = 0;
#ifdef ETL_SPEED_CHECK
    mcn->current_state.interval_time.tv_sec = 0;			// the last interval time between S <-> R transition
    mcn->current_state.interval_time.tv_nsec = 0;			// the last interval time between S <-> R transition
    mcn->current_state.max_interval_time.tv_sec = 0; 	// the max interval time
    mcn->current_state.max_interval_time.tv_nsec = 0; 	// the max interval time
    mcn->current_state.min_interval_time.tv_sec = 0;  	// the min interval time
    mcn->current_state.min_interval_time.tv_nsec = 0;  	// the min interval time
#endif

	mcn->error_state.current_state = 0 ;
  	mcn->error_state.error_flag = 0 ;
  	mcn->error_state.error_count = 0 ;

  	spin_lock_init( &mcn->state_lock ) ;

  	mcn->my_addr_valid = 0 ;
  	mcn->hello_addr_valid = 0;
} 

void etl_set_my_adder( etl_state_machine_t *mcn, __u16 u_addr, __u32 l_addr ) 
{
	spin_lock( &mcn->state_lock ) ;

	mcn->my_u_addr = u_addr ;
	mcn->my_l_addr = l_addr ;
	mcn->my_addr_valid = 1 ;
	mcn->hello_addr_valid = 0 ;

	spin_unlock( &mcn->state_lock ) ;
}

static void set_error( etl_state_machine_t *mcn, __u32 error_flag ) 
{
	struct timespec ts ;

	// Record the first error state, just count on 2nd error and after
	if( mcn->error_state.error_count == 0 ) {
	  	mcn->error_state.event_i_know = mcn->current_state.event_i_know ;
	  	mcn->error_state.event_i_sent = mcn->current_state.event_i_sent ;
	  	mcn->error_state.current_state = mcn->current_state.current_state ;
  		mcn->error_state.error_flag = error_flag ;
	  	memcpy( &mcn->error_state.update_time, &mcn->current_state.update_time, sizeof(struct timespec)) ;		
		ts = current_kernel_time();
		memcpy( &mcn->error_state.error_time, &ts, sizeof(struct timespec) ) ; 
	}
  	else{
  		mcn->error_state.p_error_flag |= error_flag ;
  	}
    mcn->error_state.error_count++ ;

}

static void clear_intervals( etl_state_machine_t *mcn )
{
#ifdef ETL_SPEED_CHECK
	memset( &mcn->current_state.interval_time, 0, sizeof(struct timespec)) ;
	memset( &mcn->current_state.max_interval_time, 0, sizeof(struct timespec)) ;
	memset( &mcn->current_state.min_interval_time, 0, sizeof(struct timespec)) ;
#endif
}


static void calc_intervals( etl_state_machine_t *mcn )
{
#ifdef ETL_SPEED_CHECK
	struct timespec ts ;
	if( mcn->current_state.update_time.tv_sec > 0 || mcn->current_state.update_time.tv_nsec > 0 ) {
		ts = current_kernel_time();
		mcn->current_state.interval_time.tv_sec = ts.tv_sec - mcn->current_state.update_time.tv_sec ;
		if( ts.tv_nsec > mcn->current_state.update_time.tv_nsec ) {
			mcn->current_state.interval_time.tv_nsec = ts.tv_nsec - mcn->current_state.update_time.tv_nsec ;
		}
		else {
			mcn->current_state.interval_time.tv_sec -= 1 ;
			mcn->current_state.interval_time.tv_nsec = 1000000000 + ts.tv_nsec - mcn->current_state.update_time.tv_nsec ;
		}
		if( mcn->current_state.max_interval_time.tv_sec < mcn->current_state.interval_time.tv_sec ||
		   ( mcn->current_state.max_interval_time.tv_sec == mcn->current_state.interval_time.tv_sec && 
		   	 mcn->current_state.max_interval_time.tv_nsec < mcn->current_state.interval_time.tv_nsec) )
		{
			mcn->current_state.max_interval_time.tv_sec = mcn->current_state.interval_time.tv_sec ;
			mcn->current_state.max_interval_time.tv_nsec = mcn->current_state.interval_time.tv_nsec ;
		}
		if( (mcn->current_state.min_interval_time.tv_sec == 0 && mcn->current_state.min_interval_time.tv_nsec == 0 ) ||
			mcn->current_state.min_interval_time.tv_sec > mcn->current_state.interval_time.tv_sec ||
			( mcn->current_state.min_interval_time.tv_sec == mcn->current_state.interval_time.tv_sec && 
		   	 mcn->current_state.min_interval_time.tv_nsec > mcn->current_state.interval_time.tv_nsec) )
		{
			mcn->current_state.min_interval_time.tv_sec = mcn->current_state.interval_time.tv_sec ;
			mcn->current_state.min_interval_time.tv_nsec = mcn->current_state.interval_time.tv_nsec ;
		}
	}
#endif
}

__u32 get_etl_state( etl_state_machine_t *mcn ) 
{
	__u16 ret ;

	spin_lock( &mcn->state_lock ) ;

	if( mcn->error_state.error_count ) {
		ret = ETL_STATE_ERROR ;
	}
	else {
		ret = mcn->current_state.current_state ;
	}

	spin_unlock( &mcn->state_lock ) ;
	
	return ret ;

}

void etl_received( etl_state_machine_t *mcn, __u16 u_saddr, __u32 l_saddr, __u16 u_daddr, __u32 l_daddr ) 
{
	struct timespec ts ;

	if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_NOP_U) {
		return ;
	}
	if( mcn->my_addr_valid == 0 ) {
			// say error here
		ETL_DEBUG( "message received without my address set" ) ;
		return ;		
	}

	spin_lock( &mcn->state_lock ) ;

	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {

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
				mcn->hello_u_addr = u_saddr ;
				mcn->hello_l_addr = l_saddr ;
				mcn->hello_addr_valid = 1 ;
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
				if( mcn->my_u_addr > u_saddr || (mcn->my_u_addr == u_saddr && mcn->my_l_addr > l_saddr ) ) {
					mcn->current_state.event_i_sent = mcn->current_state.event_i_know = mcn->current_state.event_send_next = 0 ;
					mcn->current_state.current_state = ETL_STATE_SEND ;		
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals( mcn ) ; 
				}
				else if( mcn->my_u_addr == u_saddr && mcn->my_l_addr == l_saddr ) {
					// say error as Alan's 1990s problem again
					ETL_DEBUG( "hello message with same address received @ %ld sec", ts.tv_sec ) ;
					set_error( mcn, ETL_ERROR_SAME_ADDRESS ) ;
					mcn->current_state.current_state = ETL_STATE_IDLE ;		
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				}
				else {
					mcn->current_state.event_i_sent = mcn->current_state.event_i_know = mcn->current_state.event_send_next = 0 ;
					mcn->current_state.current_state = ETL_STATE_RECEIVE ;			
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals( mcn ) ; 
				}			// case we received before sending hello, which seems sequence error
			}
			else {
				// Received non hello message on Wait state
				ETL_DEBUG( "non-hello message %d received on hello state @ %ld sec", u_saddr, ts.tv_sec ) ;			
				set_error( mcn, ETL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.current_state = ETL_STATE_HELLO ;		
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;			
			}		
		}
		break ;
		case ETL_STATE_SEND:
		{
			if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_EVENT_U || (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U
				) {
				set_error( mcn, ETL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.current_state = ETL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			}
		}
		break ;
		case ETL_STATE_RECEIVE:
		{
			if( u_daddr == ETL_MESSAGE_EVENT_U ) {
				if( mcn->current_state.event_i_know == 0 && mcn->current_state.event_i_sent == 0 && mcn->current_state.event_send_next == 0 )
				{
					// fresh out of Hello handshake
					if( l_daddr == 0 ) {
						mcn->current_state.event_i_know = l_daddr ;
						mcn->current_state.event_send_next = l_daddr + 1 ;
						mcn->current_state.current_state = ETL_STATE_SEND ;
						calc_intervals( mcn ) ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						set_error( mcn, ETL_ERROR_FLAG_SEQUENCE ) ;
						mcn->current_state.current_state = ETL_STATE_HELLO ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					}

				}
				else {
					if( mcn->current_state.event_i_sent + 1 != l_daddr ) {
						set_error( mcn, ETL_ERROR_FLAG_SEQUENCE ) ;
						mcn->current_state.current_state = ETL_STATE_HELLO ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						mcn->current_state.event_i_know = l_daddr ;
						mcn->current_state.event_send_next = l_daddr + 1 ;
						mcn->current_state.current_state = ETL_STATE_SEND ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
				}
			}
			else if( (u_daddr & ETL_MESSAGE_MASK) == ETL_MESSAGE_HELLO_U ) {
				set_error( mcn, ETL_ERROR_FLAG_SEQUENCE ) ;
				mcn->hello_u_addr = u_saddr ;
				mcn->hello_l_addr = l_saddr ;
				mcn->hello_addr_valid = 1 ;
				mcn->current_state.current_state = ETL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			}

		}
		break ;
		default:
		{
			set_error( mcn, ETL_ERROR_UNKOWN_STATE ) ;
			mcn->current_state.current_state = ETL_STATE_IDLE ;		
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;			
		}
		break ;
	}
	spin_unlock( &mcn->state_lock ) ;
}

void etl_next_send( etl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) 
{
	struct timespec ts ;

	spin_lock( &mcn->state_lock ) ;

	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {

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
			if( mcn->hello_addr_valid ) {
				// case peer hello is already received
				if( mcn->my_addr_valid == 0 ) {
					// say error here
					ETL_DEBUG( "My address is not set on Hello message request @ %ld sec", ts.tv_sec ) ;			
				}
				else {
					if( mcn->my_u_addr > mcn->hello_u_addr || (mcn->my_u_addr == mcn->hello_u_addr && mcn->my_l_addr > mcn->hello_l_addr ) ) {
						mcn->current_state.current_state = ETL_STATE_SEND ;			
						mcn->current_state.event_i_sent = mcn->current_state.event_i_know = mcn->current_state.event_send_next = 0 ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
						clear_intervals( mcn ) ; 
					}
					else if( mcn->my_u_addr == mcn->hello_u_addr && mcn->my_l_addr == mcn->hello_l_addr ) {
						// say error as Alan's 1990s problem again
						ETL_DEBUG( "hello message with same address received @ %ld sec", ts.tv_sec ) ;
						set_error( mcn, ETL_ERROR_SAME_ADDRESS ) ;
						mcn->current_state.current_state = ETL_STATE_IDLE ;		
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					}
					else {
						mcn->current_state.current_state = ETL_STATE_RECEIVE ;			
						mcn->current_state.event_i_sent = mcn->current_state.event_i_know = mcn->current_state.event_send_next = 0 ;
						memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
						clear_intervals( mcn ) ; 
					}
					mcn->hello_addr_valid = 0 ;	
				}
			}
			else {
				mcn->current_state.current_state = ETL_STATE_WAIT ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
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
			
			mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
			mcn->current_state.event_send_next += 2 ;
			*l_addr = mcn->current_state.event_i_sent ;
			mcn->current_state.current_state = ETL_STATE_RECEIVE ;			
			calc_intervals( mcn ) ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;

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
	spin_unlock( &mcn->state_lock ) ;

}

void etl_state_error( etl_state_machine_t *mcn, __u32 error_flag ) 
{
	spin_lock( &mcn->state_lock ) ;

 	set_error( mcn, error_flag ) ;

	spin_unlock( &mcn->state_lock ) ;
}

etl_state_t* etl_read_current_state( etl_state_machine_t *mcn ) 
{
	spin_lock( &mcn->state_lock ) ;

  	memcpy( &mcn->return_state, &mcn->error_state, sizeof(etl_state_t)) ;

	spin_unlock( &mcn->state_lock ) ;	
	return &mcn->return_state ;
}

etl_state_t* etl_read_error_state( etl_state_machine_t *mcn ) 
{
	spin_lock( &mcn->state_lock ) ;
	
  	memcpy( &mcn->return_state, &mcn->error_state, sizeof(etl_state_t)) ;
  	mcn->error_state.error_count = 0 ;

	spin_unlock( &mcn->state_lock ) ;	
	return &mcn->return_state ;
}

void etl_link_up( etl_state_machine_t *mcn ) 
{
	struct timespec ts ;

	spin_lock( &mcn->state_lock ) ;

	ts = current_kernel_time();

	if( mcn->current_state.current_state == ETL_STATE_IDLE ) {
		ETL_DEBUG( "Link UP !! @ %ld sec", ts.tv_sec ) ;
		mcn->current_state.current_state = ETL_STATE_HELLO ;
		memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;		
	}
	else {
		ETL_DEBUG( "Unexpected Link UP on state %d @ %ld sec", mcn->current_state.current_state, ts.tv_sec ) ;
		set_error( mcn, ETL_ERROR_UNEXPECTED_LU ) ;
		mcn->current_state.current_state = ETL_STATE_HELLO ;
		memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;		
	}
	spin_unlock( &mcn->state_lock ) ;		
}

