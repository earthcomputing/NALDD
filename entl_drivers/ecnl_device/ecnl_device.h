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

#define ENCL_FW_TABLE_ENTRY_ARRAY	15
#define ECNL_FW_TABLE_ENTRY_SIZE	(sizeof(u32)*(ENCL_FW_TABLE_ENTRY_ARRAY+1))

#define ECNL_TABLE_NUM_ENTRIES	(ECNL_TABLE_WORD_SIZE*8)/ECNL_TABLE_BIT_SIZE

typedef struct ecnl_table_entry 
{
	union {
		struct {
			unsigned int reserved : 12 ;
			unsigned int parent : 4 ;
			unsigned int port_vector : 16 ;
		} ;
		u32 raw_vector ;
	}
	u32 nextID[ENCL_FW_TABLE_ENTRY_ARRAY] ;
} ecnl_table_entry_t ;

struct entl_driver_funcs {
	netdev_tx_t		(*start_xmit)(struct sk_buff *skb, struct net_device *dev);
	int (*get_entl_state) (  struct net_device *dev, struct entl_ioctl_data *state) ;
	int (*send_AIT_message)( struct net_device *dev, struct entt_ioctl_ait_data* data ) ;
	int (retrieve_AIT_message)( struct net_device *dev, struct entt_ioctl_ait_data* data ) ;
	int (next_AIT_message)( struct net_device *dev, struct entt_ioctl_ait_data* data ) ;
} ;

#define ENTL_NAME_MAX_LEN 80 ;

struct entl_driver {
	unsigned char *name ;
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
	ecnl_table_entry *current_table ;
	u32 current_table_size ;
	ecnl_table_entry *ecnl_tables[ENTL_TABLE_MAX] ;
	u32 ecnl_tables_size[ENTL_TABLE_MAX] ;

	bool fw_enable ;

	struct net_device_stats net_stats ;

	spinlock_t drivers_lock ;       // spin lock to access driver entry
	u16 num_drivers ;
    struct entl_driver drivers[ENTL_DRIVER_MAX] ;
    struct entl_driver *drivers_map[ENTL_DRIVER_MAX] ;

    u16 u_addr ;
    u32 l_addr ;

} ;

// interface function table to lower drivers
struct ecnl_device_funcs {
	int (*driver_index)( unsigned char *encl_name ) ;
	int (*register_driver)( int encl_id, unsigned char *name, struct net_device *device, u16 u_addr, u32 l_addr, struct entl_driver_funcs *funcs ) ;
	int (*receive_skb)(int encl_id, int drv_index, struct sk_buff *skb) ;
	int (*receive_dsc)(int encl_id, int drv_index, struct sk_buff *skb) ;
	void (*link_status_update)( int encl_id, int drv_index, struct entl_ioctl_data *state ) ;
	void (*got_ait_message)( int encl_id, int drv_index, struct entt_ioctl_ait_data* data ) ;
	void (*deregister_drivers)( int encl_id ) ;
} ;

#endif
