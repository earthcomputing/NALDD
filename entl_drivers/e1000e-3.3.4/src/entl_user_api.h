/* 
 * ENTL User API 
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
// #include <linux/time.h>

#include "entl_state_defs.h"

#ifndef _ENTL_USER_API_H_
#define _ENTL_USER_API_H_

// Enable to calcurate speed of ENTL message exchange
#define ENTL_SPEED_CHECK

// Error type bits
#define ENTL_ERROR_FLAG_SEQUENCE 0x0001
#define ENTL_ERROR_FLAG_LINKDONW 0x0002
#define ENTL_ERROR_FLAG_TIMEOUT  0x0004
#define ENTL_ERROR_SAME_ADDRESS	0x0008
#define ENTL_ERROR_UNKOWN_CMD	  0x0010
#define ENTL_ERROR_UNKOWN_STATE	0x0020
#define ENTL_ERROR_UNEXPECTED_LU 0x0040
#define ENTL_ERROR_FATAL         0x8000

#ifndef u32
#define u32 unsigned int
#endif
 
// The data structre represents the internal state of ENTL
typedef struct entl_state {
  u32 event_i_know ;			// last received event number 
  u32 event_i_sent ;			// last event number sent
  u32 event_send_next ;		// next event number sent
  u32 current_state ;			// 0: idle  1: H 2: W 3:S 4:R
  struct timespec update_time ;		// last updated time in microsecond resolution
  u32 error_flag ;				// first error flag 
  u32 p_error_flag ;				// when more than 1 error is detected, those error bits or ored to this flag
  u32 error_count ;				// Count multiple error, cleared in got_error_state()
  struct timespec error_time	; 		// the time the first error detected in microsecond resorution
#ifdef ENTL_SPEED_CHECK
  struct timespec interval_time	; 		// the last interval time between S <-> R transition
  struct timespec max_interval_time	; 	// the max interval time
  struct timespec min_interval_time	; 	// the min interval time
#endif
} entl_state_t ;

/*
 * Using ioctl values for device private. 
 *  The comment in sockios.h says this is deprecated and disapper in 2.5.x... 
 *  Hope this works.....
 */
// #define SIOCDEVPRIVATE	0x89F0	/* to 89FF */

#define SIOCDEVPRIVATE_ENTL_RD_CURRENT		0x89F0
#define SIOCDEVPRIVATE_ENTL_RD_ERROR			0x89F1
#define SIOCDEVPRIVATE_ENTL_SET_SIGRCVR		0x89F2

// For testing purpose, extra ioctls are created
#define SIOCDEVPRIVATE_ENTL_GEN_SIGNAL      0x89F3
#define SIOCDEVPRIVATE_ENTL_DO_INIT   0x89F4

// ENTT send/read AIT ioctls 
#define SIOCDEVPRIVATE_ENTT_SEND_AIT   0x89F5
#define SIOCDEVPRIVATE_ENTT_READ_AIT   0x89F6

/* This structure is used in all of SIOCDEVPRIVATE_ENTL_xxx ioctl calls */
struct entl_ioctl_data {
	int				pid;    // set own uid for signal
  int       link_state ; // 0: down, 1: up
  entl_state_t    state ;
  entl_state_t    error_state ;
  u32   icr ;
  u32   ctrl ;
  u32   ims ;
};

/* This structure is used in all of SIOCDEVPRIVATE_ENTT_xxx ioctl calls */
#define MAX_AIT_MESSAGE_SIZE 256 

struct entt_ioctl_ait_data {
  u32 num_messages ;
  u32 message_len ;
  char data[MAX_AIT_MESSAGE_SIZE] ;
};


#endif

