/* 
 * ETL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *
 */
#include <linux/time.h>
#ifndef _ETL_STATE_MACHINE_H_
#define _ETL_STATE_MACHINE_H_

// State definition
#define ETL_STATE_IDLE 0
#define ETL_STATE_HELLO 1
#define ETL_STATE_WAIT 2
#define ETL_STATE_SEND 3
#define ETL_STATE_RECEIVE 4
#define ETL_STATE_ERROR 5

// Message sent on the MAC dst addr
#define ETL_MESSAGE_HELLO_U  0x0000
#define ETL_MESSAGE_HELLO_L  0x0
#define ETL_MESSAGE_EVENT_U  0x0001
#define ETL_MESSAGE_NOP_U	 0x0002

// Error type bits
#define ETL_ERROR_FLAG_SEQUENCE 1
#define ETL_ERROR_FLAG_LINKDONW 2
#define ETL_ERROR_FLAG_TIMEOUT  4

// The data structre represents the internal state of ETL
typedef struct etl_state {
  uint32_t event_i_know ;			// last received event number 
  uint32_t event_i_sent ;			// last event number sent
  uint32_t event_send_next ;		// next event number sent
  uint32_t current_state ;			// 0: idle  1: H 2: W 3:S 4:R
  struct timeval update_time ;		// last updated time in microsecond resolution
  uint32_t error_flag ;				// individual bit indicate the error type as 1: sequence error, 2: link down 4: timeout
  uint32_t error_count ;			// Count multiple error, cleared in got_error_state()
  struct timeval error_time	; 		// the time the last error detected in microsecond resorution
} etl_state_t ;

//void etl_state_update( uint32_t state ) ;
// My Mac address must be set at the beginning of operation
void etl_set_my_adder( uint16_t u_addr, uint32_t l_addr ) ; 
// On Hello message, this should be called with MAC source address on the packet
void etl_hello_received( uint16_t u_addr, uint32_t l_addr ) ; 
// On other message, this should be called with the massage (MAC destination addr)
void etl_received( uint16_t u_addr, uint32_t l_addr ) ; 
// On sending ethernet packet, this function should be called to get the destination MAC address for message
void etl_next_send( uint16_t *u_addr, uint32_t *l_addr ) ; 
// On receiving error (link down, timeout), this functon should be called to report to the state machine
void etl_state_error( uint32_t error_flag ) ;
// quick refrence to get the current state. It will return error when error is reported until the error state is read via etl_read_error_state
uint32_t get_etl_state() ;

// returns the pointer to the etl_state that shows current state
etl_state_t* etl_read_current_state() ;
// returns the pointer to the etl_state that the state when error is detected.
etl_state_t* etl_read_error_state() ;

#endif