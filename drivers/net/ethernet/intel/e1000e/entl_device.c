/* 
 * ENTL Device 
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 *    Note: This code is written as .c but actually included in a part of netdevice.c in e1000e driver code
 *     so that it can share the static functions in the driver.
 */
 // #include "entl_device.h"

/// function to inject min-size message for ENTL
//    it returns 0 if success, 1 if need to retry due to resource, -1 if fatal 
//
//    ToDo:  need a mutex for the tx_ring access
//
static int inject_message( entl_device_t *dev, __u16 u_addr, __u32 l_addr )
{
	struct e1000_adapter *adapter = container_of( dev, e1000_adapter, entl_dev );
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_tx_desc *tx_desc = NULL;
	struct e1000_buffer *buffer_info;
	struct sk_buff *skb;
    struct e1000_ring *tx_ring = adapter->tx_ring ;
	unsigned char d_addr[ETH_ALEN] ;
	u32 txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;

	if (test_bit(__E1000_DOWN, &adapter->state)) return 1 ;
	if( e1000_desc_unused(tx_ring) < 3 ) return 1 ; 

	d_addr[0] = (u_addr >> 8) | 0x80 ; // set messege only flag
	d_addr[1] = u_addr ;
	d_addr[2] = l_addr >> 24 ;
	d_addr[3] = l_addr >> 16;
	d_addr[4] = l_addr >> 8;
	d_addr[5] = l_addr ;

	skb = __netdev_alloc_skb( netdev, ETH_ZLEN + ETH_FCS_LEN, GFP_ATOMIC );
	if( skb ) {
		struct ethhdr *eth = (struct ethhdr *)skb->data ;
		skb->len = ETH_ZLEN + ETH_FCS_LEN ;     // min packet size + crc
		memcpy(eth->h_source, netdev->dev_addr, ETH_ALEN);
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
		eth->h_proto = 0 ; // protocol type is not used anyway
		int i = adapter->tx_ring->next_to_use;
		buffer_info = &tx_ring->buffer_info[i];
		buffer_info->length = skb->len;
		buffer_info->time_stamp = jiffies;
		buffer_info->next_to_watch = i;
		buffer_info->dma = dma_map_single(&pdev->dev,
						  skb->data + offset,
						  size, DMA_TO_DEVICE);
		buffer_info->mapped_as_page = false;
		if (dma_mapping_error(&pdev->dev, buffer_info->dma))
		{
			ENTL_DEBUG("ENTL inject_message failed map dma\n");
			buffer_info->dma = 0;
			dev_kfree_skb_any(skb);
			return -1 ;
		}
		buffer_info->skb = skb ;
		// report number of byte queued for sending to the device hardware queue
		netdev_sent_queue(netdev, skb->len);
		// process e1000_tx_queue
		tx_desc = E1000_TX_DESC(*tx_ring, i);
		tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		tx_desc->lower.data = cpu_to_le32(txd_lower |
						  buffer_info->length);
		tx_desc->upper.data = cpu_to_le32(txd_upper);
		tx_desc->lower.data |= cpu_to_le32(adapter->txd_cmd);

		i++;
		if (i == tx_ring->count) i = 0;
		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();

		tx_ring->next_to_use = i;

		// Update TDT register in the NIC
		if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
			e1000e_update_tdt_wa(tx_ring,
						     tx_ring->next_to_use);
		else
			writel(tx_ring->next_to_use, tx_ring->tail);

		/* we need this if more than one processor can write
		 * to our tail at a time, it synchronizes IO on
		 *IA64/Altix systems
		 */
		mmiowb();
	}
	else {
		ENTL_DEBUG("ENTL inject_message failed to allocate sk_buffer\n");
		return -1 ;
	}
	return 0 ;
}

/**
 * entl_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void entl_watchdog(unsigned long data)
{
	entl_device_t *dev = (entl_device_t *)data;

	/* Do the rest outside of interrupt context */
	schedule_work(&dev->watchdog_task);           // schedule task using the global kernel work queue

}

static void entl_watchdog_task(struct work_struct *work)
{
	unsigned long wakeup = 1 * HZ ;  // one second

	entl_device_t *dev = container_of(work, entl_device_t, watchdog_task); // get the struct pointer from a member
	if( (dev->flag & ENTL_DEVICE_FLAG_SIGNAL) && dev->user_pid ) {
		struct siginfo info;
		struct task_struct *t;
		info.si_signo=SIGIO;
		info.si_int=1;
		info.si_code = SI_QUEUE;        
		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_SIG ;
		t= pid_task(find_vpid(dev->user_pid),PIDTYPE_PID);//user_pid has been fetched successfully
		if(t == NULL){
		        ENTL_DEBUG("ENTL no such pid, cannot send signal\n");
		} else {
		        ENTL_DEBUG("ENTL found the task, sending SIGUSR1 signal\n");
		        send_sig_info(SIGUSR1, &info, t);
		}
	}
	if( dev->flag & ENTL_DEVICE_FLAG_HELLO ) {
		struct e1000_adapter *adapter = container_of( dev, e1000_adapter, entl_dev );
    	struct e1000_ring *tx_ring = adapter->tx_ring ;
		entl_state_t st ;
		entl_read_current_state( &dev->stm, &st ) ;
		if (test_bit(__E1000_DOWN, &adapter->state)) goto restart_watchdog ;
		if( e1000_desc_unused(tx_ring) < 3 ) goto restart_watchdog ; 
		if( st.current_state == ENTL_STATE_HELLO ) {
			__u16 u_addr ;
			__u32 l_addr ;
			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_HELLO ;
			if( entl_get_hello(&dev->stm, &u_addr, &l_addr) ){
				// attomically check to make sure we still need to send hello
				inject_message( dev, u_addr, l_addr ) ;
			}
		}
	}
	restart_watchdog:
	mod_timer(&dev->watchdog_timer, round_jiffies(jiffies + wakeup));
}

static void entl_device_init( entl_device_t *dev ) 
{
	// initialize the state machine
	entl_state_machine_init( &dev->stm ) ;

	dev->user_pid = 0 ;
	dev->flag = 0 ;

  	spin_lock_init( &dev->tx_ring_lock ) ;

	// watchdog timer & task setup
	init_timer(&dev->watchdog_timer);
	dev->watchdog_timer.function = entl_watchdog;
	dev->watchdog_timer.data = (unsigned long)dev;
	INIT_WORK(&dev->watchdog_task, entl_watchdog_task);

}

static void entl_device_link_up( entl_device_t *dev ) 
{
	entl_link_up( dev->stm ) ;
	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
}

static void entl_device_link_down( entl_device_t *dev ) 
{
	entl_state_error( dev->stm, ENTL_ERROR_FLAG_LINKDONW ) ;
	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
}

static void entl_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd) 
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	entl_device_t *dev = adapter->entl_device ;
	struct entl_ioctl_data *data = (struct entl_ioctl_data *) &ifr->ifr_ifru;
	entl_state_t st ;
	switch( cmd )
	{
	case SIOCDEVPRIVATE_ENTL_RD_CURRENT:
		ENTL_DEBUG("ENTL ioctl reading current state\n" );
		st = entl_read_current_state( &dev->stm, &st ) ;
		copy_to_user(&data->state, &st, sizeof(entl_state_t));
		break;		
	case SIOCDEVPRIVATE_ENTL_RD_ERROR:
		ENTL_DEBUG("ENTL ioctl reading error state\n" );
		st = entl_read_error_state( &dev->stm, &st ) ;
		copy_to_user(&data->state, &st, sizeof(entl_state_t));
		break;
	case SIOCDEVPRIVATE_ENTL_SET_SIGRCVR:
		ENTL_DEBUG("ENTL ioctl user_pid %d is set\n", data->pid );
		dev->user_pid = data->pid ;
		break;
	default:
		ENTL_DEBUG("ENTL ioctl error: undefined cmd %d\n", cmd);
		break;
	}
}

static void entl_do_user_signal( struct e1000_adapter *adapter ) 
{
	entl_device_t *dev = adapter->entl_device ;

	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
}

// process received packet, if not message only, return true to let upper side forward this packet
//   It is assumed that this is called on ISR context.
static bool entl_device_process_rx_packet( entl_device_t *dev, struct sk_buff *skb )
{
	bool ret = true ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	int result ;

    __u16 s_u_addr; 
    __u32 s_l_addr	
    __u16 d_u_addr; 
    __u32 d_l_addr	

    s_u_addr = (__u16)eth->h_source[0] << 8 | eth->h_source[1] ;
    s_l_addr = (__u32)eth->h_source[2] << 24 | (__u32)eth->h_source[3] << 16 | (__u32)eth->h_source[4] << 0 | (__u32)eth->h_source[5] ;
    d_u_addr = (__u16)eth->h_dest[0] << 8 | eth->h_dest[1] ;
    d_l_addr = (__u32)eth->h_dest[2] << 24 | (__u32)eth->h_dest[3] << 16 | (__u32)eth->h_dest[4] << 0 | (__u32)eth->h_dest[5] ;

    if( d_u_addr & ENTL_MESSAGE_ONLY_U ) ret = false ; // this is message only packet

    result = entl_received( dev->stm, s_u_addr, s_l_addr, d_u_addr, d_l_addr ) ;

    if( result == 1 ) {
    	// need to send message
    	// this is ISR version to get the next to send
    	entl_next_send_ir( dev->mcn, &d_u_addr, &d_l_addr ) ;
    	if( d_u_addr & ENTL_MESSAGE_MASK != ENTL_MESSAGE_NOP_U ) {  // last minute check
    		unsigned long flags ;
			spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
    		result = inject_message_ir( dev, d_u_addr, d_l_addr ) ;
    		spin_unlock_irqsave( &adapter->tx_ring_lock, flags ) ;
    		// if failed to inject message, so invoke the task
    		if( result == 1 ) {
    			// resource error, so retry
    			dev->u_addr = d_u_addr ;
    			dev->l_addr = d_l_addr ;
    			dev->flag |= ENTL_DEVICE_FLAG_RETRY ;
				mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
    		}
    		else if( result == -1 ) {
     			entl_state_error( dev->mcn, ENTL_ERROR_FATAL ) ;
   				dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
				mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
    		}
    	}
    }
    else if( result == -1 ) {
    	// error, need to send signal & hello, 
		dev->flag |= ENTL_DEVICE_FLAG_HELLO | ENTL_DEVICE_FLAG_SIGNAL ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
	}

	return ret ;

}

// process packet being sent. The ENTL message can only be ent over the single (non MSS) packet
//  Assuming this is called from non-interrupt context
static void entl_device_process_tx_packet( entl_device_t *dev, struct sk_buff *skb )
{
	__u16 u_addr;
	__u32 l_addr;
	unsigned char d_addr[ETH_ALEN] ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;

	if( skb_is_gso(skb) ) {
		// MSS packet can't be used for ENTL message (will use a header over multiple packets)
		u_addr = ENTL_MESSAGE_NOP_U ;
		l_addr = 0 ;  
		d_addr[0] = (u_addr >> 8) ; 
		d_addr[1] = u_addr ;
		d_addr[2] = l_addr >> 24 ;
		d_addr[3] = l_addr >> 16;
		d_addr[4] = l_addr >> 8;
		d_addr[5] = l_addr ;		
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
	}
	else {
		entl_next_send( dev->mcn, &u_addr, &l_addr ) ;
		d_addr[0] = (u_addr >> 8) ; 
		d_addr[1] = u_addr ;
		d_addr[2] = l_addr >> 24 ;
		d_addr[3] = l_addr >> 16;
		d_addr[4] = l_addr >> 8;
		d_addr[5] = l_addr ;		
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
	}

}