/* 
 * ENTL Device 
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
 #ifndef _ENTL_DEVICE_H_
#define _ENTL_DEVICE_H_

 #include "entl_state_machine.h"

// these flags are used to request tasks to service task
#define ENTL_DEVICE_FLAG_HELLO 		1
#define ENTL_DEVICE_FLAG_SIGNAL 	2
#define ENTL_DEVICE_FLAG_RETRY 		4
#define ENTL_DEVICE_FLAG_WAITING 	8
#define ENTL_DEVICE_FLAG_SIGNAL2	0x10

#define ENTL_DEVICE_FLAG_FATAL 0x8000

 #define ENTL_DEFAULT_TXD   256


typedef struct ENTL_skb_queue {
    u16 size ;
    u16 count ;
    u16 head ;
    u16 tail ;
    struct sk_buff *data[ENTL_DEFAULT_TXD] ;
} ENTL_skb_queue_t ;

typedef struct entl_device {
	entl_state_machine_t stm ;              /// the state machine structure

	struct timer_list watchdog_timer;      /// watchdog time for this device
	struct work_struct watchdog_task;      /// kernel process task for non-interrupt processes
	
	int user_pid;                          /// user process id to send the signal

	// flag is used to set a request to the service task
	__u32 flag ;

	// keep the last value to be sent for retry
    __u16 u_addr; 
    __u32 l_addr;	
    int action ;

    char name[ENTL_DEVICE_NAME_LEN] ;

  	ENTL_skb_queue_t tx_skb_queue ;
  	int queue_stopped ;

} entl_device_t ;

// entl_device.c is also included in the netdev.c code so all functions are declared static here

#ifdef _IN_NETDEV_C_

/// initialize the entl device structure
static void entl_device_init( entl_device_t *dev ) ;

/// handle link up 
static void entl_device_link_up( entl_device_t *dev ) ;

/// handle link down 
static void entl_device_link_down( entl_device_t *dev ) ;

/// handle the ioctl request specific to ENTL driver
static int entl_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd) ;

/// send signal to the user process
// static void entl_do_user_signal( entl_device_t *dev ) ;

/// process the packet upon receive
static bool entl_device_process_rx_packet( entl_device_t *dev, struct sk_buff *skb ) ;

/// process the packet for transmit. 
static void entl_device_process_tx_packet( entl_device_t *dev, struct sk_buff *skb ) ;

/// entl version of e1000_configure - configure the hardware for Rx and Tx
static void entl_e1000_configure(struct e1000_adapter *adapter) ;

static void entl_e1000_set_my_addr( entl_device_t *dev, const u8 *addr ) ;


static int entl_tx_queue_has_data( entl_device_t *dev ) ;

/// tx queue handling
static netdev_tx_t entl_tx_transmit( struct sk_buff *skb, struct net_device *netdev ) ;

/// send tx queue data to tx_ring
static void entl_tx_pull( struct net_device *netdev ) ;

#endif    /* _IN_NETDEV_C_ */


#endif