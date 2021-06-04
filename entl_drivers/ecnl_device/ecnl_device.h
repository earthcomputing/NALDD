/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#ifndef _ECNL_DEVICE_H_
#define _ECNL_DEVICE_H_

#include "ecnl_user_api.h"
#include "ec_user_api.h"

// Real driver should always use this name to access ECNL level driver
#define MAIN_DRIVER_NAME	"ecnl0"

// Debug print control
#define ECNL_DEBUG_PRINT_ENABLE


#ifdef ECNL_DEBUG_PRINT_ENABLE
#define ECNL_DEBUG(fmt, args...) printk( KERN_ALERT "ECNL:" fmt, ## args )
#else
#define ECNL_DEBUG(fmt, args...) /* no message */
#endif

// FW TABLE carries 16bit port vector (upper 16bit unused) and 32bit FW_Index
#define ECNL_FW_TABLE_VECTOR_MASK	0xFFFF

#define ECNL_FW_TABLE_ENTRY_SIZE	(sizeof(u32)*(ENCL_FW_TABLE_ENTRY_ARRAY+1))

#define ECNL_TABLE_NUM_ENTRIES	(ECNL_TABLE_WORD_SIZE*8)/ECNL_TABLE_BIT_SIZE

struct entl_driver_funcs {
	netdev_tx_t		(*start_xmit)(struct sk_buff *skb, struct net_device *dev);
	int (*get_entl_state) (  struct net_device *dev, ec_state_t *state) ;
	int (*send_AIT_message)( struct sk_buff *skb, struct net_device *dev ) ;
	int (*retrieve_AIT_message)( struct net_device *dev, ec_ait_data_t* data ) ;
	int (*write_alo_reg)( struct net_device *dev, ec_alo_reg_t* reg ) ;
	int (*read_alo_regs)( struct net_device *dev, ec_alo_regs_t* regs ) ;
} ;

#define ENTL_NAME_MAX_LEN 80 ;

struct entl_driver {
	unsigned char *name ;
	int index ;  // index in this device array
	struct net_device *device ;
	struct entl_driver_funcs *funcs ;
} ;

// for simiulation, we can create multiple instances of ENCL driver
#define ECNL_DRIVER_MAX 1024

#define ENTL_TABLE_MAX 16
#define ENTL_DRIVER_MAX 16

#define ECNL_NAME_LEN 20

struct ecnl_device 
{
	unsigned char name[ECNL_NAME_LEN] ;
	int index ;
	ecnl_table_entry_t *current_table ;
	u32 current_table_size ;
	ecnl_table_entry_t *ecnl_tables[ENTL_TABLE_MAX] ;
	u32 ecnl_tables_size[ENTL_TABLE_MAX] ;

	bool fw_enable ;

	u32 fw_map[ENCL_FW_TABLE_ENTRY_ARRAY] ;

	struct net_device_stats net_stats ;

	spinlock_t drivers_lock ;       // spin lock to access driver entry
	u16 num_ports ;
    struct entl_driver drivers[ENTL_DRIVER_MAX] ;
    //struct entl_driver *drivers_map[ENTL_DRIVER_MAX] ;

} ;

// interface function table to lower drivers
struct ecnl_device_funcs {
	int (*register_port)( int encl_id, unsigned char *name, struct net_device *device, struct entl_driver_funcs *funcs ) ;
	int (*receive_skb)(int encl_id, int drv_index, struct sk_buff *skb) ;
	//int (*receive_dsc)(int encl_id, int drv_index, struct sk_buff *skb) ;
	void (*link_status_update)( int encl_id, int drv_index, struct ec_state *state ) ;
	void (*forward_ait_message)( int encl_id, int drv_index, struct sk_buff *skb ) ;
	void (*got_ait_massage)( int encl_id, int drv_index, int num_message ) ;
	void (*got_alo_update)( int encl_id, int drv_index ) ;
	void (*deregister_ports)( int encl_id ) ;
} ;

#endif
