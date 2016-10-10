/* 
 * ENTL Link State Machine
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>

#include "entl_user_api.h"

#ifndef _ENTL_STATE_MACHINE_H_
#define _ENTL_STATE_MACHINE_H_

// TX handling enable
#define ENTL_TX_ON_ENTL_ENABLE

// Debug print control
#define ENTL_DEBUG_PRINT_ENABLE


#ifdef ENTL_DEBUG_PRINT_ENABLE
#define ENTL_DEBUG(fmt, args...) printk( KERN_ALERT "ENTL:" fmt, ## args )
#else
#define ENTL_DEBUG(fmt, args...) /* no message */
#endif

#define ENTL_COUNT_MAX  10
 
// Message sent on the MAC dst addr
#define ENTL_MESSAGE_HELLO_U  0x0000
#define ENTL_MESSAGE_HELLO_L  0x0000
#define ENTL_MESSAGE_EVENT_U  0x0001
#define ENTL_MESSAGE_NOP_U    0x0002
#define ENTL_MESSAGE_AIT_U    0x0003
#define ENTL_MESSAGE_ACK_U    0x0004

// When MSB of upper address is set, this is message only, no packet to upper layer
#define ENTL_MESSAGE_ONLY_U	 0x8000
#define ENTL_TEST_MASK        0x7f00
#define ENTL_MESSAGE_MASK     0x00ff

#define ENTL_DEVICE_NAME_LEN 15

#define MAX_ENTT_QUEUE_SIZE 32

typedef struct ENTT_queue {
    u16 size ;
    u16 count ;
    u16 head ;
    u16 tail ;
    void* data[MAX_ENTT_QUEUE_SIZE] ;
} ENTT_queue_t ;

/// The data structure represent the state machine
typedef struct entl_state_machine {
  entl_state_t current_state ;   // this is the current ENTL state
  entl_state_t error_state ;     // copy of current_state when error happens
  spinlock_t state_lock ;       // spin lock to access current_state

  entl_state_t return_state ;    // scratch pad state for user read

  int user_pid;                 // keep the user process id for sending error signal

  __u16 my_u_addr ;             // my MAC addr is set for Hello message
  __u32 my_l_addr ;             // my MAC addr is set for Hello message
  __u8 my_addr_valid ;          // valid flag for my MAC addr

  __u16 hello_u_addr ;          // When Hello message is received before send, the src addr is kept in here to be processed
  __u32 hello_l_addr ;          // When Hello message is received before send, the src addr is kept in here to be processed
  __u8 hello_addr_valid;        // valid flag for hello address

  __u32 state_count ;

  struct entt_ioctl_ait_data* receive_buffer ;

  ENTT_queue_t send_ATI_queue ;
  ENTT_queue_t receive_ATI_queue ;

  char name[ENTL_DEVICE_NAME_LEN] ;

} entl_state_machine_t ;


/// initialize the state machine structure
void entl_state_machine_init( entl_state_machine_t *mcn ) ;

// My Mac address must be set at the beginning of operation
void entl_set_my_adder( entl_state_machine_t *mcn, __u16 u_addr, __u32 l_addr ) ; 

// Check if we need to send hello now
int entl_get_hello( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) ;

// resend hello 
// int entl_retry_hello( entl_state_machine_t *mcn ) ;

// return value flag for entl_received
#define ENTL_ACTION_NOP         0
#define ENTL_ACTION_SEND        0x01
#define ENTL_ACTION_SEND_AIT    0x02
#define ENTL_ACTION_PROC_AIT    0x04
#define ENTL_ACTION_SIG_AIT     0x08
#define ENTL_ACTION_SEND_DAT    0x10
#define ENTL_ACTION_ERROR       -1

// On Received message, this should be called with the massage (MAC source & destination addr)
//   return value : bit 0: send, bit 1: send AIT, bit 2: process AIT
int entl_received( entl_state_machine_t *mcn, __u16 u_saddr, __u32 l_saddr, __u16 u_daddr, __u32 l_daddr ) ; 

// On sending ethernet packet, this function should be called to get the destination MAC address for message
//   return value : bit 0: send, bit 1: send AIT, bit 2: process AIT
int entl_next_send( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) ; 
int entl_next_send_tx( entl_state_machine_t *mcn, __u16 *u_addr, __u32 *l_addr ) ;

// On receiving error (link down, timeout), this functon should be called to report to the state machine
void entl_state_error( entl_state_machine_t *mcn, __u32 error_flag ) ;

// quick refrence to get the current state. It will return error when error is reported until the error state is read via entl_read_error_state
__u32 get_entl_state(entl_state_machine_t *mcn) ;

// On Link-Up, this function should be called
void entl_link_up(entl_state_machine_t *mcn) ;

// Request to send the AIT message, return 0 if OK, -1 if queue full 
int entl_send_AIT_message( entl_state_machine_t *mcn, struct entt_ioctl_ait_data* data ) ;

// Read the next AIT message to send 
struct entt_ioctl_ait_data* entl_next_AIT_message( entl_state_machine_t *mcn ) ;

// the new AIT message received 
void entl_new_AIT_message( entl_state_machine_t *mcn, struct entt_ioctl_ait_data* data ) ;

// Read the AIT message, return NULL if queue empty 
struct entt_ioctl_ait_data* entl_read_AIT_message( entl_state_machine_t *mcn ) ; 

// read current state to the given state structure
void entl_read_current_state(entl_state_machine_t *mcn, entl_state_t *st, entl_state_t *err) ;
// read error state to the given state structure
void entl_read_error_state(entl_state_machine_t *mcn, entl_state_t *st, entl_state_t *err) ;

#endif