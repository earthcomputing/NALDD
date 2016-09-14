/* 
 * ENTL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/string.h>
#include "entl_state_machine.h"

static void init_ENTT_queue( ENTT_queue_t* q ) ;
static int is_ENTT_queue_full( ENTT_queue_t* q ) ;
static int push_back_ENTT_queue(ENTT_queue_t* q, void* dt ) ;
static void* front_ENTT_queue(ENTT_queue_t* q ) ;
static void* pop_front_ENTT_queue(ENTT_queue_t* q ) ;

void entl_state_machine_init( entl_state_machine_t *mcn )
{
	mcn->current_state.current_state = 0 ;
  	mcn->current_state.error_flag = 0 ;
  	mcn->current_state.error_count = 0 ;
  	// when following 3 members are all zero, it means fresh out of Hello handshake
  	mcn->current_state.event_i_sent = 0;
  	mcn->current_state.event_i_know = 0;
  	mcn->current_state.event_send_next = 0;
#ifdef ENTL_SPEED_CHECK
    mcn->current_state.interval_time.tv_sec = 0;			// the last interval time between S <-> R transition
    mcn->current_state.interval_time.tv_nsec = 0;			// the last interval time between S <-> R transition
    mcn->current_state.max_interval_time.tv_sec = 0; 	// the max interval time
    mcn->current_state.max_interval_time.tv_nsec = 0; 	// the max interval time
    mcn->current_state.min_interval_time.tv_sec = 0;  	// the min interval time
    mcn->current_state.min_interval_time.tv_nsec = 0;  	// the min interval time
#endif

	mcn->error_state.current_state = 0 ;
  	mcn->error_state.error_flag = 0 ;

  	mcn->state_count = 0 ;

  	spin_lock_init( &mcn->state_lock ) ;

  	mcn->user_pid = 0 ;
  	
  	mcn->my_addr_valid = 0 ;
  	mcn->hello_addr_valid = 0;

  	// AIT mesage handling
  	mcn->receive_buffer = NULL ;
  	init_ENTT_queue( &mcn->send_ATI_queue ) ;
  	init_ENTT_queue( &mcn->receive_ATI_queue ) ;

} 

void entl_set_my_adder( entl_state_machine_t *mcn, __u16 u_addr, __u32 l_addr ) 
{
	unsigned long flags ;
	ENTL_DEBUG( "%s set my address %04x %08x\n", mcn->name, u_addr, l_addr ) ;

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	mcn->my_u_addr = u_addr ;
	mcn->my_l_addr = l_addr ;
	mcn->my_addr_valid = 1 ;
	mcn->hello_addr_valid = 0 ;

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
}

static void set_error( entl_state_machine_t *mcn, __u32 error_flag ) 
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

static void clear_intervals( entl_state_machine_t *mcn )
{
#ifdef ENTL_SPEED_CHECK
	memset( &mcn->current_state.interval_time, 0, sizeof(struct timespec)) ;
	memset( &mcn->current_state.max_interval_time, 0, sizeof(struct timespec)) ;
	memset( &mcn->current_state.min_interval_time, 0, sizeof(struct timespec)) ;
#endif
}


static void calc_intervals( entl_state_machine_t *mcn )
{
#ifdef ENTL_SPEED_CHECK
	struct timespec ts ;
	if( mcn->current_state.update_time.tv_sec > 0 || mcn->current_state.update_time.tv_nsec > 0 ) {
		ts = current_kernel_time();
		mcn->current_state.interval_time.tv_sec = ts.tv_sec - mcn->current_state.update_time.tv_sec ;
		if( ts.tv_nsec > mcn->current_state.update_time.tv_nsec ) {
			mcn->current_state.interval_time.tv_nsec = ts.tv_nsec - mcn->current_state.update_time.tv_nsec ;
		}
		else if( mcn->current_state.interval_time.tv_sec > 1 ) {
			mcn->current_state.interval_time.tv_sec -= 1 ;
			mcn->current_state.interval_time.tv_nsec = 1000000000 + ts.tv_nsec - mcn->current_state.update_time.tv_nsec ;
		}
		else {
			//mcn->current_state.interval_time.tv_nsec = ts.tv_nsec ; // don't know what to do
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

__u32 get_entl_state( entl_state_machine_t *mcn ) 
{
	__u16 ret ;

	unsigned long flags ;
	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	if( mcn->error_state.error_count ) {
		ret = ENTL_STATE_ERROR ;
	}
	else {
		ret = mcn->current_state.current_state ;
	}

	spin_unlock( &mcn->state_lock ) ;
	
	return ret ;

}

int entl_received( entl_state_machine_t *mcn, __u16 u_saddr, __u32 l_saddr, __u16 u_daddr, __u32 l_daddr ) 
{
	struct timespec ts ;
	int retval = ENTL_ACTION_NOP ;
	unsigned long flags ;

	if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_NOP_U) {
		//ENTL_DEBUG( "%s nop message received \n", mcn->name ) ;
		return retval ;
	}


	if( mcn->my_addr_valid == 0 ) {
		// say error here
		ENTL_DEBUG( "%s message received without my address set %04x %08x\n", mcn->name, mcn->my_u_addr, mcn->my_l_addr ) ;
		return retval ;		
	}

	if( mcn->error_state.error_count ) {
		ENTL_DEBUG( "%s message %04x received on error count set %d\n", mcn->name, u_daddr, mcn->error_state.error_count ) ;
		return retval ;		
	}

	spin_lock_irqsave( &mcn->state_lock, flags ) ;
	
	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {

		case ENTL_STATE_IDLE:
		{
			// say something here as receive something on idle state
			ENTL_DEBUG( "%s message %x received @ %ld sec on Idle state!\n", mcn->name, u_daddr, ts.tv_sec ) ;
		}
		break ;
		case ENTL_STATE_HELLO:
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_HELLO_U )
			{
				mcn->hello_u_addr = u_saddr ;
				mcn->hello_l_addr = l_saddr ;
				mcn->hello_addr_valid = 1 ;
				//ENTL_DEBUG( "%s Hello message %d received on hello state @ %ld sec\n", mcn->name, u_saddr, ts.tv_sec ) ;
				if( mcn->my_u_addr > u_saddr || (mcn->my_u_addr == u_saddr && mcn->my_l_addr > l_saddr ) ) {
					mcn->current_state.event_i_sent = mcn->current_state.event_i_know = mcn->current_state.event_send_next = 0 ;
					mcn->current_state.current_state = ENTL_STATE_WAIT ;		
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals( mcn ) ; 
					retval = ENTL_ACTION_SEND ;
					mcn->state_count = 0 ;
					ENTL_DEBUG( "%s Hello message %d received on hello state and win -> Wait state @ %ld sec\n", mcn->name, u_saddr, ts.tv_sec ) ;
				}
				else if( mcn->my_u_addr == u_saddr && mcn->my_l_addr == l_saddr ) {
					// say error as Alan's 1990s problem again
					ENTL_DEBUG( "%s Fatal Error!! hello message with SAME MAC ADDRESS received @ %ld sec\n", mcn->name, ts.tv_sec ) ;
					set_error( mcn, ENTL_ERROR_SAME_ADDRESS ) ;
					mcn->current_state.current_state = ENTL_STATE_IDLE ;		
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				}
				else {
					ENTL_DEBUG( "%s Hello message %d received on wait state but not win @ %ld sec\n", mcn->name, u_saddr, ts.tv_sec ) ;
				}
			}
			else if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_EVENT_U ) {
				// Hello state got event
				if( l_daddr == 0 ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_SEND ;
					calc_intervals( mcn ) ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_SEND ;
					ENTL_DEBUG( "%s ENTL %d message received on Hello -> Send @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					ENTL_DEBUG( "%s Out of sequence ENTL %d message received on Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
			}
			else {
				// Received non hello message on Hello state
				ENTL_DEBUG( "%s non-hello message %04x received on hello state @ %ld sec\n", mcn->name, u_daddr, ts.tv_sec ) ;
			}			
		}
		break ;
		case ENTL_STATE_WAIT:
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_HELLO_U )
			{
				mcn->state_count++ ;
				if( mcn->state_count > ENTL_COUNT_MAX ) {
					ENTL_DEBUG( "%s Hello message %d received overflow %d on Wait state -> Hello state @ %ld sec\n", mcn->name, u_saddr, mcn->state_count, ts.tv_sec ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;		
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				}
			}
			else if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_EVENT_U )
			{
				if( l_daddr == mcn->current_state.event_i_sent + 1 ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_SEND ;			
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals( mcn ) ; 
					retval = ENTL_ACTION_SEND ;
					ENTL_DEBUG( "%s ENTL message %d received on Wait state -> Send state @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
				else {
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;	
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					clear_intervals( mcn ) ; 
					ENTL_DEBUG( "%s Wrong ENTL message %d received on Wait state -> Hello state @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
			}
			else {
				// Received non hello message on Wait state
				ENTL_DEBUG( "%s wrong message %04x received on Wait state -> Hello @ %ld sec\n", mcn->name, u_daddr, ts.tv_sec ) ;			
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;		
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = 0 ;		
			}		
		}
		break ;
		case ENTL_STATE_SEND:
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_EVENT_U || (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_ACK_U ) {
				if( l_daddr == mcn->current_state.event_i_know ) {
					ENTL_DEBUG( "%s Same ENTL message %d received on Send state @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ENTL %d received on Send state -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
			}
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s wrong message %04x received on Send state -> Hello @ %ld sec\n", mcn->name, u_daddr, ts.tv_sec ) ;
			}
		}
		break ;
		case ENTL_STATE_RECEIVE:
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_EVENT_U ) {
				if( mcn->current_state.event_i_know + 2 == l_daddr ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_SEND ;
					retval = ENTL_ACTION_SEND ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					//ENTL_DEBUG( "%s ETL message %d received on Receive -> Send @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else if( mcn->current_state.event_i_know == l_daddr )
				{
					ENTL_DEBUG( "%s same ETL message %d received on Receive @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ETL message %d received on Receive -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
			}
			else if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_AIT_U ) {
				if( mcn->current_state.event_i_know + 2 == l_daddr ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_AH ;
					if( is_ENTT_queue_full( &mcn->receive_ATI_queue) ) {
						ENTL_DEBUG( "%s AIT message %d received on Receive with queue full -> Ah @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
						retval = ENTL_ACTION_PROC_AIT ;
					}
					else {
						retval = ENTL_ACTION_SEND | ENTL_ACTION_PROC_AIT ;
					}
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					ENTL_DEBUG( "%s AIT message %d received on Receive -> Ah @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else if( mcn->current_state.event_i_know == l_daddr )
				{
					ENTL_DEBUG( "%s same ETL message %d received on Receive @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ETL message %d received on Receive -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
			}
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s Wrong message %04x received on Receive -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;			
			}
		}
		break ;
		// AIT 
		case ENTL_STATE_AM:     // AIT message sent, waiting for ack
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_ACK_U ) {
				if( mcn->current_state.event_i_know + 2 == l_daddr ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_BM ;
					retval = ENTL_ACTION_SEND ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					ENTL_DEBUG( "%s ETL Ack %d received on Am -> Bm @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ETL message %d received on Am -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
			}
			else if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_EVENT_U ) {
				if( mcn->current_state.event_i_know == l_daddr )
				{
					ENTL_DEBUG( "%s same ETL event %d received on Am @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Wrong message %04x received on Am -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;			
				}
			}			
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s Wrong message %04x received on Am -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;			
			}
		}
		break ;
		case ENTL_STATE_BM:  // AIT sent, Ack received, sending Ack
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_ACK_U ) {
				if( mcn->current_state.event_i_know == l_daddr )
				{
					ENTL_DEBUG( "%s same ETL Ack %d received on Bm @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Wrong message %04x received on Bm -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;			
				}
			}
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s Wrong message %04x received on Bm -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;							
			}
		}
		break ;
		case ENTL_STATE_AH:  // AIT message received, sending Ack
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_AIT_U ) {
				if( l_daddr == mcn->current_state.event_i_know ) {
					ENTL_DEBUG( "%s Same ENTL message %d received on Ah state @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ENTL %d received on Ah state -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
			}
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s wrong message %04x received on Send state -> Hello @ %ld sec\n", mcn->name, u_daddr, ts.tv_sec ) ;
			}
		}
		break ;
		case ENTL_STATE_BH:  // got AIT, Ack sent, waiting for ack
		{
			if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_ACK_U ) {
				if( mcn->current_state.event_i_know + 2 == l_daddr ) {
					mcn->current_state.event_i_know = l_daddr ;
					mcn->current_state.event_send_next = l_daddr + 1 ;
					mcn->current_state.current_state = ENTL_STATE_SEND ;
					retval = ENTL_ACTION_SEND | ENTL_ACTION_SIG_AIT ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					ENTL_DEBUG( "%s ETL Ack %d received on Bh -> Send @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
					push_back_ENTT_queue( &mcn->receive_ATI_queue, mcn->receive_buffer ) ;
					mcn->receive_buffer = NULL ;
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ETL message %d received on Am -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;			
				}
			}			
			else if( (u_daddr & ENTL_MESSAGE_MASK) == ENTL_MESSAGE_AIT_U ) {
				if( l_daddr == mcn->current_state.event_i_know ) {
					ENTL_DEBUG( "%s Same ENTL message %d received on Bh state @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
				else {
					set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
					mcn->current_state.event_i_know = 0 ;
					mcn->current_state.event_send_next = 0 ;
					mcn->current_state.event_i_sent = 0 ;
					mcn->current_state.current_state = ENTL_STATE_HELLO ;
					memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
					retval = ENTL_ACTION_ERROR ;
					ENTL_DEBUG( "%s Out of Sequence ENTL %d received on Bh state -> Hello @ %ld sec\n", mcn->name, l_daddr, ts.tv_sec ) ;
				}
			}
			else {
				set_error( mcn, ENTL_ERROR_FLAG_SEQUENCE ) ;
				mcn->current_state.event_i_know = 0 ;
				mcn->current_state.event_send_next = 0 ;
				mcn->current_state.event_i_sent = 0 ;
				mcn->current_state.current_state = ENTL_STATE_HELLO ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_ERROR ;
				ENTL_DEBUG( "%s Wrong message %04x received on Am -> Hello @ %ld sec\n", mcn->name, u_daddr,   ts.tv_sec ) ;			
			}
		}
		break ;
		default:
		{
			ENTL_DEBUG( "%s Statemachine on wrong state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
			set_error( mcn, ENTL_ERROR_UNKOWN_STATE ) ;
			mcn->current_state.event_i_know = 0 ;
			mcn->current_state.event_send_next = 0 ;
			mcn->current_state.event_i_sent = 0 ;
			mcn->current_state.current_state = ENTL_STATE_IDLE ;		
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;			
		}
		break ;
	}
	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;

	//ENTL_DEBUG( "%s entl_received Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			


	return retval ;
}

int entl_get_hello( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr )
{
	struct timespec ts ;
	int ret = ENTL_ACTION_NOP ;
	unsigned long flags ;

	if( mcn->error_state.error_count ) {
		ENTL_DEBUG( "%s entl_get_hello called on error count set %d\n", mcn->name, mcn->error_state.error_count ) ;
		return ret ;		
	}

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {
		case ENTL_STATE_HELLO:
		{
			*l_addr = ENTL_MESSAGE_HELLO_L ;
			*u_addr = ENTL_MESSAGE_HELLO_U ;
			ret = ENTL_ACTION_SEND ;
		}
		break ;
		case ENTL_STATE_WAIT:
		{
			//ENTL_DEBUG( "%s repeated Message requested on Wait state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_EVENT_U ;
			ret = ENTL_ACTION_SEND ;
		}
		break ;
		case ENTL_STATE_RECEIVE:
		{
			ENTL_DEBUG( "%s repeated Message requested on Receive state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = mcn->current_state.event_i_sent ;
			*u_addr = ENTL_MESSAGE_EVENT_U ;
			ret = ENTL_ACTION_SEND ;
		}
		break ;
		case ENTL_STATE_AM:
		{
			ENTL_DEBUG( "%s repeated AIT requested on Am state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = mcn->current_state.event_i_sent ;
			*u_addr = ENTL_MESSAGE_AIT_U ;
			ret = ENTL_ACTION_SEND | ENTL_ACTION_SEND_AIT ;
		}
		break ;		
		case ENTL_STATE_BH:
		{
			if( is_ENTT_queue_full( &mcn->receive_ATI_queue) ) {

			}
			else {
				ENTL_DEBUG( "%s repeated Ack requested on Bh state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
				*l_addr = mcn->current_state.event_i_sent ;
				*u_addr = ENTL_MESSAGE_ACK_U ;
				ret = ENTL_ACTION_SEND ;
			}
		}
		break ;
		default:
		break ;
	}

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_get_hello Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
	return ret ;
}

// not used 
//int entl_retry_hello( entl_state_machine_t *mcn ) 
//{
//	struct timespec ts ;
//	int ret = 0 ;
//	unsigned long flags ;
//	spin_lock_irqsave( &mcn->state_lock, flags ) ;
//
//	ts = current_kernel_time();
//	switch( mcn->current_state.current_state ) {
//		case ENTL_STATE_WAIT:
//		{
//			ENTL_DEBUG( "%s entl_retry_hello move state to hello @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
//			mcn->current_state.current_state = ENTL_STATE_HELLO ;
//			ret = 1 ;
//		}
//		break ;
//		default:
//		break ;
//	}
//	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
//	ENTL_DEBUG( "%s entl_retry_hello Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
//	return ret ;
//
//}

int entl_next_send( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) 
{
	int retval = ENTL_ACTION_NOP ;
	unsigned long flags ;
	struct timespec ts ;

	if( mcn->error_state.error_count ) {
		ENTL_DEBUG( "%s entl_get_hello called on error count set %d\n", mcn->name, mcn->error_state.error_count ) ;
		return retval ;		
	}

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {

		case ENTL_STATE_IDLE:
		{
			// say something here as attempt to send something on idle state
			ENTL_DEBUG( "%s Message requested on Idle state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		case ENTL_STATE_HELLO:
		{
			//ENTL_DEBUG( "%s repeated Message requested on Hello state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = ENTL_MESSAGE_HELLO_L ;
			*u_addr = ENTL_MESSAGE_HELLO_U ;
			retval = ENTL_ACTION_SEND ;
		}
		break ;
		case ENTL_STATE_WAIT:
		{
			//ENTL_DEBUG( "%s repeated Message requested on Wait state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_EVENT_U ;
		}
		break ;
		case ENTL_STATE_SEND:
		{
			mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
			mcn->current_state.event_send_next += 2 ;
			*l_addr = mcn->current_state.event_i_sent ;
			calc_intervals( mcn ) ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			if( mcn->send_ATI_queue.count ) {
				mcn->current_state.current_state = ENTL_STATE_AM ;			
				*u_addr = ENTL_MESSAGE_AIT_U ;
				retval = ENTL_ACTION_SEND | ENTL_ACTION_SEND_AIT ;
				ENTL_DEBUG( "%s ETL AIT Message %d requested on Send state -> Am @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			
			}
			else {
				mcn->current_state.current_state = ENTL_STATE_RECEIVE ;			
				*u_addr = ENTL_MESSAGE_EVENT_U ;
				retval = ENTL_ACTION_SEND ;
			}
			//ENTL_DEBUG( "%s ETL Message %d requested on Send state -> Receive @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			

		}
		break ;
		case ENTL_STATE_RECEIVE:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		// AIT 
		case ENTL_STATE_AM:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		case ENTL_STATE_BM:
		{
			struct entt_ioctl_ait_data* ait_data ;
			mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
			mcn->current_state.event_send_next += 2 ;
			*l_addr = mcn->current_state.event_i_sent ;
			*u_addr = ENTL_MESSAGE_ACK_U ;
			calc_intervals( mcn ) ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			retval = ENTL_ACTION_SEND ;
			mcn->current_state.current_state = ENTL_STATE_RECEIVE ;
			// drop the message on the top
			ait_data = pop_front_ENTT_queue( &mcn->send_ATI_queue ) ;
			if( ait_data ) {
				kfree(ait_data) ;
			}
			ENTL_DEBUG( "%s ETL AIT ACK %d requested on BM state -> Receive @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			
		}
		break ;
		case ENTL_STATE_AH:
		{
			if( is_ENTT_queue_full( &mcn->receive_ATI_queue) ) {
				*l_addr = 0 ;
				*u_addr = ENTL_MESSAGE_NOP_U ;
			}
			else {
				mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
				mcn->current_state.event_send_next += 2 ;
				*l_addr = mcn->current_state.event_i_sent ;
				*u_addr = ENTL_MESSAGE_ACK_U ;
				calc_intervals( mcn ) ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_SEND ;
				mcn->current_state.current_state = ENTL_STATE_BH ;			
				ENTL_DEBUG( "%s ETL AIT ACK %d requested on Ah state -> Bh @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			
			}
		}
		break ;
		case ENTL_STATE_BH:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		default:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ; 
	}

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_next_send Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
	return retval ;
}

// For TX, it can't send AIT, so just keep ENTL state on Send state
int entl_next_send_tx( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) 
{
	unsigned long flags ;
	struct timespec ts ;
	int retval = ENTL_ACTION_NOP ;

	ts = current_kernel_time();

	if( mcn->error_state.error_count ) {
		ENTL_DEBUG( "%s entl_get_hello called on error count set %d\n", mcn->name, mcn->error_state.error_count ) ;
		return retval ;		
	}
	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	ts = current_kernel_time();

	switch( mcn->current_state.current_state ) {

		case ENTL_STATE_IDLE:
		{
			// say something here as attempt to send something on idle state
			ENTL_DEBUG( "%s Message requested on Idle state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		case ENTL_STATE_HELLO:
		{
			//ENTL_DEBUG( "%s repeated Message requested on Hello state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = ENTL_MESSAGE_HELLO_L ;
			*u_addr = ENTL_MESSAGE_HELLO_U ;
			retval = ENTL_ACTION_SEND ;
		}
		break ;
		case ENTL_STATE_WAIT:
		{
			//ENTL_DEBUG( "%s repeated Message requested on Wait state @ %ld sec\n", mcn->name, ts.tv_sec ) ;			
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_EVENT_U ;
		}
		break ;
		case ENTL_STATE_SEND:
		{
			mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
			mcn->current_state.event_send_next += 2 ;
			*l_addr = mcn->current_state.event_i_sent ;
			calc_intervals( mcn ) ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			mcn->current_state.current_state = ENTL_STATE_RECEIVE ;			
			*u_addr = ENTL_MESSAGE_EVENT_U ;
			retval = ENTL_ACTION_SEND ;
			// For TX, it can't send AIT, so just keep ENTL state on Send state
			//ENTL_DEBUG( "%s ETL Message %d requested on Send state -> Receive @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			

		}
		break ;
		case ENTL_STATE_RECEIVE:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		// AIT 
		case ENTL_STATE_AM:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		case ENTL_STATE_BM:
		{
			mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
			mcn->current_state.event_send_next += 2 ;
			*l_addr = mcn->current_state.event_i_sent ;
			*u_addr = ENTL_MESSAGE_ACK_U ;
			calc_intervals( mcn ) ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
			retval = ENTL_ACTION_SEND ;
			mcn->current_state.current_state = ENTL_STATE_RECEIVE ;
			// drop the message on the top
			pop_front_ENTT_queue( &mcn->send_ATI_queue ) ;		
			ENTL_DEBUG( "%s ETL AIT ACK %d requested on BM state -> Receive @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			
		}
		break ;
		case ENTL_STATE_AH:
		{
			if( is_ENTT_queue_full( &mcn->receive_ATI_queue) ) {
				*l_addr = 0 ;
				*u_addr = ENTL_MESSAGE_NOP_U ;
			}
			else {
				mcn->current_state.event_i_sent = mcn->current_state.event_send_next ;
				mcn->current_state.event_send_next += 2 ;
				*l_addr = mcn->current_state.event_i_sent ;
				*u_addr = ENTL_MESSAGE_ACK_U ;
				calc_intervals( mcn ) ;
				memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;
				retval = ENTL_ACTION_SEND ;
				mcn->current_state.current_state = ENTL_STATE_BH ;			
				ENTL_DEBUG( "%s ETL AIT ACK %d requested on Ah state -> Bh @ %ld sec\n", mcn->name, *l_addr, ts.tv_sec ) ;			
			}
		}
		break ;
		case ENTL_STATE_BH:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ;
		default:
		{
			*l_addr = 0 ;
			*u_addr = ENTL_MESSAGE_NOP_U ;
		}
		break ; 
	}
	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_next_send Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
	return retval ;
}

void entl_state_error( entl_state_machine_t *mcn, __u32 error_flag ) 
{
	unsigned long flags ;
	struct timespec ts ;

	if( error_flag == ENTL_ERROR_FLAG_LINKDONW && mcn->current_state.current_state == ENTL_STATE_IDLE ) {
	 	return ;
	}

	ts = current_kernel_time();
	spin_lock_irqsave( &mcn->state_lock, flags ) ;

 	set_error( mcn, error_flag ) ;

 	if( error_flag == ENTL_ERROR_FLAG_LINKDONW ) {
	 	mcn->current_state.current_state = ENTL_STATE_IDLE ;
	}
	else if( error_flag == ENTL_ERROR_FLAG_SEQUENCE ) {
		mcn->current_state.current_state = ENTL_STATE_HELLO ;
		memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;		
	  	mcn->current_state.error_flag = 0 ;
	  	mcn->current_state.error_count = 0 ;
	  	// when following 3 members are all zero, it means fresh out of Hello handshake
	  	mcn->current_state.event_i_sent = 0;
	  	mcn->current_state.event_i_know = 0;
	  	mcn->current_state.event_send_next = 0;
#ifdef ENTL_SPEED_CHECK
	    mcn->current_state.interval_time.tv_sec = 0;			// the last interval time between S <-> R transition
	    mcn->current_state.interval_time.tv_nsec = 0;			// the last interval time between S <-> R transition
	    mcn->current_state.max_interval_time.tv_sec = 0; 	// the max interval time
	    mcn->current_state.max_interval_time.tv_nsec = 0; 	// the max interval time
	    mcn->current_state.min_interval_time.tv_sec = 0;  	// the min interval time
	    mcn->current_state.min_interval_time.tv_nsec = 0;  	// the min interval time
#endif		 		
 	}

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	ENTL_DEBUG( "%s entl_state_error %d Statemachine exit on state %d on %ld sec\n", mcn->name, error_flag, mcn->current_state.current_state, ts.tv_sec ) ;			
}

void entl_read_current_state( entl_state_machine_t *mcn, entl_state_t *st, entl_state_t *err ) 
{
	unsigned long flags ;
	struct timespec ts ;
	ts = current_kernel_time();
	spin_lock_irqsave( &mcn->state_lock, flags ) ;

  	memcpy( st, &mcn->current_state, sizeof(entl_state_t)) ;
  	memcpy( err, &mcn->error_state, sizeof(entl_state_t)) ;

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_read_current_state Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
}

void entl_read_error_state( entl_state_machine_t *mcn, entl_state_t *st, entl_state_t *err ) 
{
	unsigned long flags ;
	struct timespec ts ;
	ts = current_kernel_time();
	spin_lock_irqsave( &mcn->state_lock, flags ) ;
	
  	memcpy( st, &mcn->current_state, sizeof(entl_state_t)) ;
  	memcpy( err, &mcn->error_state, sizeof(entl_state_t)) ;
  	memset(&mcn->error_state, 0, sizeof(entl_state_t)) ;
  	
	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_read_error_state Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
}

void entl_link_up( entl_state_machine_t *mcn ) 
{
	unsigned long flags ;
	struct timespec ts ;
	ts = current_kernel_time();

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	ts = current_kernel_time();

	if( mcn->current_state.current_state == ENTL_STATE_IDLE ) {
		if( mcn->error_state.error_count ) {
			ENTL_DEBUG( "%s got Link UP with error count %d @ %ld sec ignored\n", mcn->name, mcn->error_state.error_count, ts.tv_sec ) ;
		}
		else {
			ENTL_DEBUG( "%s Link UP !! @ %ld sec\n", mcn->name, ts.tv_sec ) ;
			mcn->current_state.current_state = ENTL_STATE_HELLO ;
			memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;		
		  	mcn->current_state.error_flag = 0 ;
		  	mcn->current_state.error_count = 0 ;
		  	// when following 3 members are all zero, it means fresh out of Hello handshake
		  	mcn->current_state.event_i_sent = 0;
		  	mcn->current_state.event_i_know = 0;
		  	mcn->current_state.event_send_next = 0;
#ifdef ENTL_SPEED_CHECK
		    mcn->current_state.interval_time.tv_sec = 0;			// the last interval time between S <-> R transition
		    mcn->current_state.interval_time.tv_nsec = 0;			// the last interval time between S <-> R transition
		    mcn->current_state.max_interval_time.tv_sec = 0; 	// the max interval time
		    mcn->current_state.max_interval_time.tv_nsec = 0; 	// the max interval time
		    mcn->current_state.min_interval_time.tv_sec = 0;  	// the min interval time
		    mcn->current_state.min_interval_time.tv_nsec = 0;  	// the min interval time
#endif
		}
	}
	else {
		ENTL_DEBUG( "Unexpected Link UP on state %d @ %ld sec ignored\n", mcn->current_state.current_state, ts.tv_sec ) ;
		//set_error( mcn, ENTL_ERROR_UNEXPECTED_LU ) ;
		//mcn->current_state.current_state = ENTL_STATE_HELLO ;
		//memcpy( &mcn->current_state.update_time, &ts, sizeof(struct timespec)) ;		
	}
	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;
	//ENTL_DEBUG( "%s entl_link_up Statemachine exit on state %d on %ld sec\n", mcn->name, mcn->current_state.current_state, ts.tv_sec ) ;			
}

// AIT handling functions
// Request to send the AIT message, return 0 if OK, -1 if queue full 
int entl_send_AIT_message( entl_state_machine_t *mcn, struct entt_ioctl_ait_data* data ) 
{
	int ret ;
	unsigned long flags ;

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	ret = push_back_ENTT_queue( &mcn->send_ATI_queue, (void*)data  ) ;

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;

	return ret ;
}

// Read the next AIT message to send 
struct entt_ioctl_ait_data* entl_next_AIT_message( entl_state_machine_t *mcn ) 
{
	struct entt_ioctl_ait_data* dt ;
	unsigned long flags ;

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	dt = (struct entt_ioctl_ait_data*)front_ENTT_queue( &mcn->send_ATI_queue ) ;

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;

	return dt ;	
}

// the new AIT message received 
void entl_new_AIT_message( entl_state_machine_t *mcn, struct entt_ioctl_ait_data* data ) 
{
	unsigned long flags ;

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	mcn->receive_buffer = data ;

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;

}

// Read the AIT message, return NULL if queue empty 
struct entt_ioctl_ait_data* entl_read_AIT_message( entl_state_machine_t *mcn ) 
{
	struct entt_ioctl_ait_data* dt ;
	unsigned long flags ;

	spin_lock_irqsave( &mcn->state_lock, flags ) ;

	dt = pop_front_ENTT_queue( &mcn->receive_ATI_queue ) ;
	if( dt ) {
		dt->num_messages = mcn->receive_ATI_queue.count ;  // return how many left
	}

	spin_unlock_irqrestore( &mcn->state_lock, flags ) ;

	return dt ;	
}


static void init_ENTT_queue( ENTT_queue_t* q ) 
{
    q->size = MAX_ENTT_QUEUE_SIZE ;
    q->count = 0 ;
    q->head = q->tail = 0 ;
}

static int is_ENTT_queue_full( ENTT_queue_t* q ) 
{
    if( q->size == q->count ) return 1 ;
    return 0 ;
}

static int push_back_ENTT_queue(ENTT_queue_t* q, void* dt ) 
{
    if( q->size == q->count ) {
    	// queue full
    	return -1 ;
    }
    q->data[q->tail] = dt ;
    q->tail = (q->tail+1) % q->size ;
    q->count++ ;
    return q->size - q->count ;
}

static void* front_ENTT_queue(ENTT_queue_t* q ) 
{
	void *dt ;
	if( q->count == 0 ) return NULL ; // queue empty
	dt = q->data[q->head] ;
    return dt ;
}

static void* pop_front_ENTT_queue(ENTT_queue_t* q ) 
{
	void *dt ;
	if( q->count == 0 ) return NULL ; // queue empty
	dt = q->data[q->head] ;
    q->head = (q->head+1) % q->size ;
    q->count-- ;
    return dt ;
}
