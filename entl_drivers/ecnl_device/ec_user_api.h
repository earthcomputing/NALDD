/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
// #include <linux/time.h>

#ifndef _EC_USER_API_H_
#define _EC_USER_API_H_

#define ETH_P_ECLP  0xEAC0    /* Earth Computing Link Protocol [ NOT AN OFFICIALLY REGISTERED ID ] */
#define ETH_P_ECLD  0xEAC1    /* Earth Computing Link Discovery [ NOT AN OFFICIALLY REGISTERED ID ] */
#define ETH_P_ECLL  0xEAC2    /* Earth Computing Link Local Data  [ NOT AN OFFICIALLY REGISTERED ID ] */

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
typedef struct ec_state {
  uint64_t recover_count ;      // how many recover happened
  uint64_t recovered_count ;      // how many recovered happened
  uint64_t s_count ;      // how many s message happened
  uint64_t r_count ;      // how many r message happened
  uint64_t entt_count ;         // how many entt transaction happened
  uint64_t aop_count ;          // how many aop transaction happened
  int link_state ;            // link  state
  int num_queued ;            // num AIT messages
  struct timespec update_time ;		// last updated time in microsecond resolution
#ifdef ENTL_SPEED_CHECK
  struct timespec interval_time	; 		// the last interval time between S <-> R transition
  struct timespec max_interval_time	; 	// the max interval time
  struct timespec min_interval_time	; 	// the min interval time
#endif
} ec_state_t ;

/* This structure is used in all of SIOCDEVPRIVATE_ENTT_xxx ioctl calls */
#define MAX_AIT_MESSAGE_SIZE 256 

typedef struct ec_ait_data {
  u32 message_len ;
  char data[MAX_AIT_MESSAGE_SIZE] ;
  //u32 op_code ;                  // AOP instruction
} ec_ait_data_t ;

typedef struct ec_alo_reg {
  u32 index ;
  uint64_t reg ;
} ec_alo_reg_t ;

typedef struct ec_alo_regs {
  uint64_t regs[32] ;
  uint32_t flags ;
} ec_alo_regs_t ;

#endif

