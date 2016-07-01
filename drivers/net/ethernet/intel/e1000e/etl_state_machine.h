/* 
 * ETL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *
 */
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>

#ifndef _ETL_STATE_MACHINE_H_
#define _ETL_STATE_MACHINE_H_

// Enable to calcurate speed of ETL message exchange
#define ETL_SPEED_CHECK

// Debug print control
#define ETL_DEBUG_PRINT_ENABLE


#ifdef ETL_DEBUG_PRINT_ENABLE
#define ETL_DEBUG(fmt, args...) printk( KERN_DEBUG "ETL:" fmt, ## args )
#else
#define ETL_DEBUG(fmt, args...) /* no message */
#endif

// State definition
#define ETL_STATE_IDLE 0
#define ETL_STATE_HELLO 1
#define ETL_STATE_WAIT 2
#define ETL_STATE_SEND 3
#define ETL_STATE_RECEIVE 4
#define ETL_STATE_ERROR 5

// Message sent on the MAC dst addr
#define ETL_MESSAGE_HELLO_U  0x0000
#define ETL_MESSAGE_HELLO_L  0x0000
#define ETL_MESSAGE_EVENT_U  0x0001
#define ETL_MESSAGE_NOP_U	 0x0002
// When MSB of upper address is set, this is message only, no packet to upper layer
#define ETL_MESSAGE_ONLY_U	 0x8000
#define ETL_MESSAGE_MASK     0x7fff

// Error type bits
#define ETL_ERROR_FLAG_SEQUENCE 0x0001
#define ETL_ERROR_FLAG_LINKDONW 0x0002
#define ETL_ERROR_FLAG_TIMEOUT  0x0004
#define ETL_ERROR_SAME_ADDRESS	0x0008
#define ETL_ERROR_UNKOWN_CMD	0x0010
#define ETL_ERROR_UNKOWN_STATE	0x0020
#define ETL_ERROR_UNEXPECTED_LU	0x0040

// The data structre represents the internal state of ETL
typedef struct etl_state {
  __u32 event_i_know ;			// last received event number 
  __u32 event_i_sent ;			// last event number sent
  __u32 event_send_next ;		// next event number sent
  __u32 current_state ;			// 0: idle  1: H 2: W 3:S 4:R
  struct timespec update_time ;		// last updated time in microsecond resolution
  __u32 error_flag ;				// first error flag 
  __u32 p_error_flag ;				// when more than 1 error is detected, those error bits or ored to this flag
  __u32 error_count ;				// Count multiple error, cleared in got_error_state()
  struct timespec error_time	; 		// the time the first error detected in microsecond resorution
#ifdef ETL_SPEED_CHECK
  struct timespec interval_time	; 		// the last interval time between S <-> R transition
  struct timespec max_interval_time	; 	// the max interval time
  struct timespec min_interval_time	; 	// the min interval time
#endif
} etl_state_t ;

/// The data structure represent the state machine
typedef struct etl_state_machine {
  etl_state_t state ;
  etl_state_t error_state ;
  spinlock_t state_lock ;

  etl_state_t return_state ;

  __u16 my_u_addr ;
  __u32 my_l_addr ;
  __u8 my_addr_valid ;

  __u16 hello_u_addr ;
  __u32 hello_l_addr ;
  __u8 hello_addr_valid;
} etl_state_machine_t ;

/// initialize the state machine structure
void etl_state_machine_init( etl_state_machine_t *mcn ) ;

// My Mac address must be set at the beginning of operation
void etl_set_my_adder( etl_state_machine_t *mcn, __u16 u_addr, __u32 l_addr ) ; 
// On Received message, this should be called with the massage (MAC source & destination addr)
void etl_received( etl_state_machine_t *mcn, __u16 u_saddr, __u32 l_saddr, __u16 u_daddr, __u32 l_daddr ) ; 
// On sending ethernet packet, this function should be called to get the destination MAC address for message
void etl_next_send( etl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) ; 
// On receiving error (link down, timeout), this functon should be called to report to the state machine
void etl_state_error( etl_state_machine_t *mcn, __u32 error_flag ) ;
// quick refrence to get the current state. It will return error when error is reported until the error state is read via etl_read_error_state
__u32 get_etl_state(etl_state_machine_t *mcn) ;
// On Link-Up, this function should be called
void etl_link_up(etl_state_machine_t *mcn) ;


// returns the pointer to the etl_state that shows current state
etl_state_t* etl_read_current_state(etl_state_machine_t *mcn) ;
// returns the pointer to the etl_state that the state when error is detected.
etl_state_t* etl_read_error_state(etl_state_machine_t *mcn) ;

#endif