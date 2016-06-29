/* 
 * ETL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/spinlock.h>
#include <linux/string.h>
#include "etl_state_machine.h"

spinlock_t state_lock = SPIN_LOCK_UNLOCKED ;

static etl_state_t current_state =
  { .current_state = 0 ; 
  	.error_flag = 0 ;
  	.error_count = 0 ;
  	// when following 3 members are all zero, it means fresh out of Hello handshake
  	.event_i_sent = 0 ;
  	.event_i_know = 0 ;
  	.event_send_next = 0 ;
  };
static etl_state_t error_state =
  { .current_state = 0 ; 
  	.error_flag = 0 ;
  	.error_count = 0 ;
  };

static etl_state_t return_state ;
static uint16_t my_u_addr ;
static uint32_t my_l_addr ;
static uint8_t my_addr_valid = 0 ;

static uint16_t hello_u_addr ;
static uint32_t hello_l_addr ;
static uint8_t hello_addr_valid = 0;

void etl_set_my_adder( uint16_t u_addr, uint32_t l_addr ) 
{
	spin_lock( &state_lock ) ;

	my_u_addr = u_addr ;
	my_l_addr = l_addr ;
	my_addr_valid = 1 ;
	hello_addr_valid = 0 ;

	spin_unlock( &state_lock ) ;
}

static void set_error( uint32_t error_flag ) 
{
	// Record the first error state, just count on 2nd error and after
	if( error_state.error_count == 0 ) {
	  	error_state.event_i_know = current_state.event_i_know ;
	  	error_state.event_i_sent = current_state.event_i_sent ;
	  	error_state.current_state = current_state.current_state ;
	  	memcpy( &error_state.update_time, &current_state.update_time, sizeof(struct timeval)) ;		
		do_gettimeofday( &error_state.error_time, NULL ) ; 
	}
  	error_state.error_flag |= error_flag ;
    error_state.error_count++ ;

}

void etl_hello_received( uint16_t u_addr, uint32_t l_addr ) 
{
	if( my_addr_valid == 0 ) {
		// say error here
		return ;
	}
	spin_lock( &state_lock ) ;
	if( current_state.current_state == ETL_STATE_WAIT ) {
		if( my_u_addr > u_addr || (my_u_addr == u_addr && my_l_addr > l_addr ) ) {
			current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
			current_state.current_state = ETL_STATE_SEND ;			
			do_gettimeofday( &current_state.update_time, NULL ) ; 
		}
		else if( my_u_addr == u_addr && my_l_addr == l_addr ) {
			// say error as Alan's 1990s problem again
		}
		else {
			current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
			current_state.current_state = ETL_STATE_RECEIVE ;			
			do_gettimeofday( &current_state.update_time, NULL ) ; 
		}
	}
	else if( current_state.current_state == ETL_STATE_HELLO ) {
		// case the hellow came first before attempt to send hello
		hello_u_addr = u_addr ;
		hello_l_addr = l_addr ;
		hello_addr_valid = 1 ;
	}
	else {
		if( current_state.current_state != ETL_STATE_IDLE ) {
			set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
			current_state.current_state = ETL_STATE_HELLO ;			
			do_gettimeofday( &current_state.update_time, NULL ) ;
		} 		
	}

	spin_unlock( &state_lock ) ;

}

uint32_t get_etl_state() 
{
	uint16_t ret ;

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

void etl_received( uint16_t u_addr, uint32_t l_addr ) 
{
	if( u_addr == ETL_MESSAGE_NOP_U) {
		return ;
	}

	spin_lock( &state_lock ) ;

	switch( current_state.current_state ) {

		case ETL_STATE_IDLE:
		{
			// say something here as receive something on idle state
		}
		break ;
		case ETL_STATE_HELLO:
		{
			// case we received before sending hello, which seems sequence error
			set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
			current_state.current_state = ETL_STATE_IDLE ;
			do_gettimeofday( &current_state.update_time, NULL ) ; 
		}
		break ;
		case ETL_STATE_WAIT:
		{
			set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
			current_state.current_state = ETL_STATE_IDLE ;
			do_gettimeofday( &current_state.update_time, NULL ) ; 
		}
		break ;
		case ETL_STATE_SEND:
		{
			if( u_addr == ETL_MESSAGE_EVENT_U ) {
				set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
				current_state.current_state = ETL_STATE_HELLO ;
				do_gettimeofday( &current_state.update_time, NULL ) ; 
			}
		}
		break ;
		case ETL_STATE_RECEIVE:
		{
			if( u_addr == ETL_MESSAGE_EVENT_U ) {
				if( current_state.event_i_know == 0 && current_state.event_i_sent == 0 && current_state.event_send_next == 0 )
				{
					// fresh out of Hello handshake
					if( l_addr == 0 ) {
						current_state.event_i_know = l_addr ;
						current_state.event_send_next = l_addr + 1 ;
						current_state.current_state = ETL_STATE_SEND ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 		
					}
					else {
						set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
						current_state.current_state = ETL_STATE_HELLO ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 						
					}

				}
				else {
					if( current_state.event_i_sent + 1 != l_addr ) {
						set_error( ETL_ERROR_FLAG_SEQUENCE ) ;
						current_state.current_state = ETL_STATE_HELLO ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 
					}
					else {
						current_state.event_i_know = l_addr ;
						current_state.event_send_next = l_addr + 1 ;
						current_state.current_state = ETL_STATE_SEND ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 						
					}
				}
			}

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

void etl_next_send( uint16_t *u_addr, uint32_t *l_addr ) 
{
	spin_lock( &state_lock ) ;
	switch( current_state.current_state ) {

		case ETL_STATE_IDLE:
		{
			// say something here as attempt to send something on idle state
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
				}
				else {
					if( my_u_addr > hello_u_addr || (my_u_addr == hello_u_addr && my_l_addr > hello_l_addr ) ) {
						current_state.current_state = ETL_STATE_SEND ;			
						current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 
					}
					else if( my_u_addr == hello_u_addr && my_l_addr == hello_l_addr ) {
						// say error as Alan's 1990s problem again
					}
					else {
						current_state.current_state = ETL_STATE_RECEIVE ;			
						current_state.event_i_sent = current_state.event_i_know = current_state.event_send_next = 0 ;
						do_gettimeofday( &current_state.update_time, NULL ) ; 
					}				
				}
			}
			else {
				current_state.current_state = ETL_STATE_WAIT ;
				do_gettimeofday( &current_state.update_time, NULL ) ; 
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
			do_gettimeofday( &current_state.update_time, NULL ) ; 

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

void etl_state_error( uint32_t error_flag ) 
{
	spin_lock( &state_lock ) ;

 	set_error( error_flag ) ;

	spin_unlock( &state_lock ) ;
}

etl_state_t* etl_read_current_state() 
{
	spin_lock( &state_lock ) ;

  	memcpy( &return_state, &error_state, sizeof(current_state)) ;

	spin_unlock( &state_lock ) ;	
	return &return_state ;
}

etl_state_t* etl_read_error_state() 
{
	spin_lock( &state_lock ) ;
	
  	memcpy( &return_state, &error_state, sizeof(etl_state_t)) ;
  	error_state.error_count = 0 ;

	spin_unlock( &state_lock ) ;	
	return &return_state ;
}
