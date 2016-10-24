/* 
 * Earth Computing Network Link Device 
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */

#ifndef _ECNL_DEVICE_H_
#define _ECNL_DEVICE_H_

#include "entl_user_api.h"

// Debug print control
#define ECNL_DEBUG_PRINT_ENABLE


#ifdef ECNL_DEBUG_PRINT_ENABLE
#define ECNL_DEBUG(fmt, args...) printk( KERN_ALERT "ECNL:" fmt, ## args )
#else
#define ECNL_DEBUG(fmt, args...) /* no message */
#endif

// use 64bit access to the table
#define ECNL_TABLE_WORD_SIZE	8
#define ECNL_TABLE_BIT_SIZE		4
#define ECNL_TABLE_NUM_ENTRIES	(ECNL_TABLE_WORD_SIZE*8)/ECNL_TABLE_BIT_SIZE

typedef u64 ecnl_table_entry ;

struct entl_driver_funcs {
	netdev_tx_t		(*start_xmit)(struct sk_buff *skb, struct net_device *dev);
	int (*send_AIT_message)( struct net_device *dev, struct entt_ioctl_ait_data* data ) ;
	struct ioctl_ait_data* (*next_AIT_message)( struct net_device *dev ) ;
} ;

struct entl_driver {
	unsigned char *name ;
	struct net_device *device ;
	struct entl_driver_funcs funcs ;
} ;

#define ECNL_TABLE_MAX 16
#define ECNL_DRIVER_MAX 15

struct ecnl_device 
{
	ecnl_table_entry **current_table ;
	u32 current_table_size ;
	ecnl_table_entry **ecnl_tables[ECNL_TABLE_MAX] ;
	u32 ecnl_tables_size[ECNL_TABLE_MAX] ;

	bool fw_enable ;

	spinlock_t drivers_lock ;       // spin lock to access driver entry
	u16 num_drivers ;
    struct entl_driver drivers[ECNL_DRIVER_MAX] ;
    struct entl_driver drivers_map[ECNL_DRIVER_MAX] ;
} ;

int ecnl_table_lookup( u16 upper, u32 lower ) ; 




#endif
