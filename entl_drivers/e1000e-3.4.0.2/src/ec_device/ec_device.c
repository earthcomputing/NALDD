/* 
 * EC Device 
 * Copyright(c) 2018 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 *    Note: This code is written as .c but actually included in a part of netdevice.c in e1000e driver code
 *     so that it can share the static functions in the driver.
 */
 // #include "ec_device.h"

static int ENTL_skb_queue_has_data( ENTL_skb_queue_t* q )  ;
static void init_ENTL_skb_queue( ENTL_skb_queue_t* q ) ;
static struct sk_buff *pop_front_ENTL_skb_queue(ENTL_skb_queue_t* q ) ;
static int ENTL_skb_queue_unused( ENTL_skb_queue_t* q ) ;

/// function to inject min-size message for ENTL
//    it returns 0 if success, 1 if need to retry due to resource, -1 if fatal 
//
//    ToDo:  need a mutex for the tx_ring access
//
static int inject_message( entl_device_t *dev, __u16 u_addr, __u32 l_addr, int flag )
{
	struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_tx_desc *tx_desc = NULL;
	struct e1000_buffer *buffer_info;
	struct sk_buff *skb;
    struct e1000_ring *tx_ring = adapter->tx_ring ;
	unsigned char d_addr[ETH_ALEN] ;
	u32 txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;
	struct entt_ioctl_ait_data* ait_data ;
	int len ;

	if (test_bit(__E1000_DOWN, &adapter->state)) return 1 ;
	if( e1000_desc_unused(tx_ring) < 3 ) return 1 ; 

	d_addr[0] = (u_addr >> 8) | 0x80 ; // set messege only flag
	d_addr[1] = u_addr ;
	d_addr[2] = l_addr >> 24 ;
	d_addr[3] = l_addr >> 16;
	d_addr[4] = l_addr >> 8;
	d_addr[5] = l_addr ;

	if( flag & ENTL_ACTION_SEND_AIT ) {
		ait_data = entl_next_AIT_message( &dev->stm ) ;
		len = ETH_HLEN + ait_data->message_len + sizeof(u32) ;
		if( len < ETH_ZLEN ) len = ETH_ZLEN ; // min length = 60 defined in include/uapi/linux/if_ether.h
		len += ETH_FCS_LEN ;
		skb = __netdev_alloc_skb( netdev, len , GFP_ATOMIC );
	}
	else {
		len = ETH_ZLEN + ETH_FCS_LEN ;
		skb = __netdev_alloc_skb( netdev, len, GFP_ATOMIC );
	}
	if( skb ) {
		int i ;
		struct ethhdr *eth = (struct ethhdr *)skb->data ;
		unsigned char *cp = skb->data + sizeof(struct ethhdr) ;
		skb->len = len ;     // min packet size + crc
		memcpy(eth->h_source, netdev->dev_addr, ETH_ALEN);
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
		eth->h_proto = 0 ; // protocol type is not used anyway
		if( flag & ENTL_ACTION_SEND_AIT ) {
			memcpy( cp, &ait_data->message_len, sizeof(u32)) ;
			memcpy( cp + sizeof(u32), ait_data->data, ait_data->message_len) ;
			ENTL_DEBUG("inject_message %02x %02x %02x %02x %02x %02x %02x %02x \n", cp[0], cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7] );

		}
		i = adapter->tx_ring->next_to_use;
		buffer_info = &tx_ring->buffer_info[i];
		buffer_info->length = skb->len;
		buffer_info->time_stamp = jiffies;
		buffer_info->next_to_watch = i;
		buffer_info->dma = dma_map_single(&pdev->dev,
						  skb->data,
						  skb->len, DMA_TO_DEVICE);
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
		//ENTL_DEBUG("ENTL inject_message %04x %08x injected on %d\n", u_addr, l_addr, i);

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
		      
	// ENTL_DEBUG("entl_watchdog_task wakes up\n");

	entl_device_t *dev = container_of(work, entl_device_t, watchdog_task); // get the struct pointer from a member
	struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );

	if( !dev->flag ) {
		dev->flag |= ENTL_DEVICE_FLAG_WAITING ;
		goto restart_watchdog ;
	}
	if( (dev->flag & ENTL_DEVICE_FLAG_SIGNAL) && dev->user_pid ) {
		struct siginfo info;
		struct task_struct *t;
		info.si_signo=SIGIO;
		info.si_int=1;
		info.si_code = SI_QUEUE;        
		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_SIGNAL ;
		t= pid_task(find_vpid(dev->user_pid),PIDTYPE_PID);//user_pid has been fetched successfully
		if(t == NULL){
		        ENTL_DEBUG("ENTL %s no such pid, cannot send signal\n", dev->name);
		} else {
		        ENTL_DEBUG("ENTL %s found the task, sending SIGUSR1 signal\n", dev->name);
		        send_sig_info(SIGUSR1, &info, t);
		}
	}
	else if( (dev->flag & ENTL_DEVICE_FLAG_SIGNAL2) && dev->user_pid ) {
		struct siginfo info;
		struct task_struct *t;
		info.si_signo=SIGIO;
		info.si_int=1;
		info.si_code = SI_QUEUE;        
		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_SIGNAL2 ;
		t= pid_task(find_vpid(dev->user_pid),PIDTYPE_PID);//user_pid has been fetched successfully
		if(t == NULL){
		        ENTL_DEBUG("ENTL %s no such pid, cannot send signal\n", dev->name);
		} else {
		        ENTL_DEBUG("ENTL %s found the task, sending SIGUSR2 signal\n", dev->name);
		        send_sig_info(SIGUSR2, &info, t);
		}
	}
	if( netif_carrier_ok(adapter->netdev) && dev->flag & ENTL_DEVICE_FLAG_HELLO ) {
		int t ;
    	struct e1000_ring *tx_ring = adapter->tx_ring ;
		//entl_state_t st ;
		ENTL_DEBUG("ENTL %s entl_watchdog_task trying to send hello\n", dev->name );
		//entl_read_current_state( &dev->stm, &st ) ;
		if (test_bit(__E1000_DOWN, &adapter->state)) {
			ENTL_DEBUG("ENTL %s entl_watchdog_task got __E1000_DOWN\n", dev->name );
			goto restart_watchdog ;
		}
		if( (t = e1000_desc_unused(tx_ring) ) < 3 ) {
			ENTL_DEBUG("ENTL %s entl_watchdog_task got t = %d\n", dev->name, t );
			goto restart_watchdog ; 
		}
		if( dev->stm.current_state.current_state == ENTL_STATE_HELLO || dev->stm.current_state.current_state == ENTL_STATE_WAIT || dev->stm.current_state.current_state == ENTL_STATE_RECEIVE || dev->stm.current_state.current_state == ENTL_STATE_AM || dev->stm.current_state.current_state == ENTL_STATE_BH) {
			__u16 u_addr ;
			__u32 l_addr ;
			int ret ;
			if( ret = entl_get_hello(&dev->stm, &u_addr, &l_addr) ){
				unsigned long flags;
				int result ;
				// attomically check to make sure we still need to send hello
				spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
	    		result = inject_message( dev, u_addr, l_addr, ret ) ;
	    		spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
	    		if( result == 0 ) {
	    			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_HELLO ;
					ENTL_DEBUG("ENTL %s entl_watchdog_task %04x %08x packet sent\n", dev->name, u_addr, l_addr );
	    		}
	    		else {
					ENTL_DEBUG("ENTL %s entl_watchdog_task hello packet failed with %d \n", dev->name, result );	    			
	    		}
 			}
 			else {
 				ENTL_DEBUG("ENTL %s entl_watchdog_task hello state lost\n", dev->name );
 			}
		}
		else {
			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_HELLO ;
			if(  dev->stm.current_state.current_state != ENTL_STATE_IDLE ){
				ENTL_DEBUG("ENTL %s entl_watchdog_task not hello/wait state but %d\n", dev->name, dev->stm.current_state.current_state );
			}
		}
	}
	else if(  dev->flag & ENTL_DEVICE_FLAG_RETRY ) {
		unsigned long flags;
		int result ;
		struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
    	struct e1000_ring *tx_ring = adapter->tx_ring ;		
		ENTL_DEBUG("ENTL %s entl_watchdog_task sending retry\n", dev->name );
		if (test_bit(__E1000_DOWN, &adapter->state)) goto restart_watchdog ;
		if( e1000_desc_unused(tx_ring) < 3 ) goto restart_watchdog ; 
		spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
	    result = inject_message( dev, dev->u_addr, dev->l_addr, dev->action ) ;
	    spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
	    if( result == 0 ) {
    		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_RETRY ;
			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry packet sent\n", dev->name );
	    }
	    else {
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry packet failed with %d \n", dev->name, result );	    			
	    }
	}
	else if( dev->flag & ENTL_DEVICE_FLAG_WAITING )
	{
		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;
		if( dev->stm.current_state.current_state == ENTL_STATE_HELLO || dev->stm.current_state.current_state == ENTL_STATE_WAIT || dev->stm.current_state.current_state == ENTL_STATE_RECEIVE || dev->stm.current_state.current_state == ENTL_STATE_AM || dev->stm.current_state.current_state == ENTL_STATE_BH ) {
			dev->flag |= ENTL_DEVICE_FLAG_HELLO ;
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry message sending\n", dev->name );
		}
	}
	restart_watchdog:
	mod_timer(&dev->watchdog_timer, round_jiffies(jiffies + wakeup));
}

static void entl_device_init( entl_device_t *dev ) 
{
	
	memset(dev, 0, sizeof(struct entl_device));

	// watchdog timer & task setup
	init_timer(&dev->watchdog_timer);
	dev->watchdog_timer.function = entl_watchdog;
	dev->watchdog_timer.data = (unsigned long)dev;
	INIT_WORK(&dev->watchdog_task, entl_watchdog_task);

	init_ENTL_skb_queue( &dev->tx_skb_queue ) ;
	dev->queue_stopped = 0 ;

	ENTL_DEBUG("ENTL entl_device_init done\n" );

}

static void entl_device_link_up( entl_device_t *dev ) 
{
	ENTL_DEBUG("ENTL entl_device_link_up called\n", dev->name );
	entl_link_up( &dev->stm ) ;
	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
	if( dev->stm.current_state.current_state ==  ENTL_STATE_HELLO) {
		dev->flag |= ENTL_DEVICE_FLAG_HELLO ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
	}
}

static void dump_state( char *type, entl_state_t *st, int flag )
{
	ENTL_DEBUG( "%s event_i_know: %d  event_i_sent: %d event_send_next: %d current_state: %d error_flag %x p_error %x error_count %d @ %ld.%ld \n", 
		type, st->event_i_know, st->event_i_sent, st->event_send_next, st->current_state, st->error_flag, st->p_error_flag, st->error_count, st->update_time.tv_sec, st->update_time.tv_nsec
	) ;
	if( st->error_flag ) {
		ENTL_DEBUG( "  Error time: %ld.%ld\n", st->error_time.tv_sec, st->error_time.tv_nsec ) ;
	}
#ifdef ENTL_SPEED_CHECK
	if( flag ) {
		ENTL_DEBUG( "  interval_time    : %ld.%ld\n", st->interval_time.tv_sec, st->interval_time.tv_nsec ) ;
		ENTL_DEBUG( "  max_interval_time: %ld.%ld\n", st->max_interval_time.tv_sec, st->max_interval_time.tv_nsec ) ;
		ENTL_DEBUG( "  min_interval_time: %ld.%ld\n", st->min_interval_time.tv_sec, st->min_interval_time.tv_nsec ) ;
	}
#endif
}

static void entl_device_link_down( entl_device_t *dev ) 
{
	ENTL_DEBUG("ENTL entl_device_link_down called\n", dev->name );
	entl_state_error( &dev->stm, ENTL_ERROR_FLAG_LINKDONW ) ;
	dev->flag = ENTL_DEVICE_FLAG_SIGNAL ;  // clear other flag and just signal
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
}

static int entl_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd) 
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	entl_device_t *dev = &adapter->entl_dev ;
	struct e1000_hw *hw = &adapter->hw;
  	struct entl_ioctl_data entl_data ;

	switch( cmd )
	{
	case SIOCDEVPRIVATE_ENTL_RD_CURRENT:
	{
		u32 link;
		//int tt ;
		struct e1000_hw *hw = &adapter->hw;
		//tt = hw->mac.get_link_status ;
    	//hw->mac.get_link_status = 1 ; // AK force to read
		link = !hw->mac.get_link_status ;
		//hw->mac.get_link_status = tt ;
		entl_data.link_state = link ;
		entl_read_current_state( &dev->stm, &entl_data.state, &entl_data.error_state ) ;
		entl_data.num_queued = entl_num_queued( &dev->stm ) ;
		//entl_data.icr = er32(ICR);
		//entl_data.ctrl = er32(CTRL);
		//entl_data.ims = er32(IMS);
		copy_to_user(ifr->ifr_data, &entl_data, sizeof(struct entl_ioctl_data));
		//ENTL_DEBUG("ENTL %s ioctl reading current state on %d icr %08x ctrl %08x ims %08x\n", dev->name, link, entl_data.icr, entl_data.ctrl, entl_data.ims );
		//dump_state( "rd current", &entl_data.state, 1 ) ;
		//dump_state( "rd error", &entl_data.error_state, 0 ) ;		
	}
		break;		
	case SIOCDEVPRIVATE_ENTL_RD_ERROR:
	{
		u32 link;
		//int tt ;
		struct e1000_hw *hw = &adapter->hw;
		//tt = hw->mac.get_link_status ;
    	//hw->mac.get_link_status = 1 ; // AK force to read
		link = !hw->mac.get_link_status ;
		//ENTL_DEBUG("ENTL %s ioctl reading error state on link %d\n", dev->name, link );
		//hw->mac.get_link_status = hw->mac.get_link_status ;
		entl_data.link_state = link ;
		entl_read_error_state( &dev->stm, &entl_data.state, &entl_data.error_state ) ;
		entl_data.num_queued = entl_num_queued( &dev->stm ) ;
		//entl_data.icr = er32(ICR);
		//entl_data.ctrl = er32(CTRL);
		//entl_data.ims = er32(IMS);
		copy_to_user(ifr->ifr_data, &entl_data, sizeof(struct entl_ioctl_data));
		dump_state( "current", &entl_data.state, 1 ) ;
		dump_state( "error", &entl_data.error_state, 0 ) ;			
	}

		break;
	case SIOCDEVPRIVATE_ENTL_SET_SIGRCVR:
		copy_from_user(&entl_data, ifr->ifr_data, sizeof(struct entl_ioctl_data) ) ;
		ENTL_DEBUG("ENTL %s ioctl user_pid %d is set\n", netdev->name, entl_data.pid );
		dev->user_pid = entl_data.pid ;
		break;
	case SIOCDEVPRIVATE_ENTL_GEN_SIGNAL:
		ENTL_DEBUG("ENTL %s ioctl got SIOCDEVPRIVATE_ENTL_GEN_SIGNAL %d %d %d \n", netdev->name, dev->tx_skb_queue.count, dev->tx_skb_queue.head, dev->tx_skb_queue.tail );
		//dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
		//mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer		
		break ;		
	case SIOCDEVPRIVATE_ENTL_DO_INIT:
		ENTL_DEBUG("ENTL %s ioctl initialize the device\n", netdev->name );
		adapter->entl_flag = 1 ;
		entl_e1000_configure( adapter ) ;
		u32 icr = er32(ICR);
		u32 ctrl = er32(CTRL);
		u32 ims = er32(IMS);
		ENTL_DEBUG("ENTL %s ioctl initialized the device with icr %08x ctrl %08x ims %08x\n", netdev->name, icr, ctrl, ims );
		break ;
	case SIOCDEVPRIVATE_ENTT_SEND_AIT:
	{
		struct entt_ioctl_ait_data* ait_data ;
		int ret ;
	    ait_data = kzalloc( sizeof(struct entt_ioctl_ait_data), GFP_ATOMIC );
		copy_from_user(ait_data, ifr->ifr_data, sizeof(struct entt_ioctl_ait_data) ) ;
		ret = entl_send_AIT_message( &dev->stm, ait_data ) ;
		ENTL_DEBUG("ENTL %s ioctl send %d byte AIT, %d left\n", netdev->name, ait_data->message_len, ret );
		ait_data->num_messages = ret ; // return how many buffer left
		copy_to_user(ifr->ifr_data, ait_data, sizeof(struct entt_ioctl_ait_data));
		if( ret < 0 ) {
			// error, dealloc data
			kfree(ait_data) ;
		}
	}
		break ;
	case SIOCDEVPRIVATE_ENTT_READ_AIT:
	{
		struct entt_ioctl_ait_data* ait_data ;
		ait_data = entl_read_AIT_message( &dev->stm ) ;
		if( ait_data ) {
			ENTL_DEBUG("ENTL %s ioctl got %d byte AIT, %d left\n", netdev->name, ait_data->message_len, ait_data->num_messages );
			copy_to_user(ifr->ifr_data, ait_data, sizeof(struct entt_ioctl_ait_data));
			kfree(ait_data) ;
		}
		else {
			struct entt_ioctl_ait_data dt ;
			dt.num_messages = 0 ;
			dt.message_len = 0 ;
			dt.num_queued = entl_num_queued( &dev->stm ) ;
			copy_to_user(ifr->ifr_data, &dt, sizeof(struct entt_ioctl_ait_data));
		}
	}
		break ;
	default:
		ENTL_DEBUG("ENTL %s ioctl error: undefined cmd %d\n", netdev->name, cmd);
		break;
	}
	return 0 ;
}

//static void entl_do_user_signal( entl_device_t *dev ) 
//{

//	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
//	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
//}

// process received packet, if not message only, return true to let upper side forward this packet
//   It is assumed that this is called on ISR context.
static bool entl_device_process_rx_packet( entl_device_t *dev, struct sk_buff *skb )
{
	struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
	bool retval = true ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	int result ;

    u16 s_u_addr; 
    u32 s_l_addr;	
    u16 d_u_addr; 
    u32 d_l_addr;	

    s_u_addr = (u16)eth->h_source[0] << 8 | eth->h_source[1] ;
    s_l_addr = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
    d_u_addr = (u16)eth->h_dest[0] << 8 | eth->h_dest[1] ;
    d_l_addr = (u32)eth->h_dest[2] << 24 | (u32)eth->h_dest[3] << 16 | (u32)eth->h_dest[4] << 8 | (u32)eth->h_dest[5] ;

    if( d_u_addr & ENTL_MESSAGE_ONLY_U ) retval = false ; // this is message only packet

	else ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got %d s: %04x %08x d: %04x %08x t:%04x\n", skb->len, adapter->netdev->name, s_u_addr, s_l_addr, d_u_addr, d_l_addr, eth->h_proto );

    result = entl_received( &dev->stm, s_u_addr, s_l_addr, d_u_addr, d_l_addr ) ;

	//ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got entl_received result %d\n", dev->name, result);
    if( result == ENTL_ACTION_ERROR ) {
    	// error, need to send signal & hello, 
		dev->flag |= ENTL_DEVICE_FLAG_HELLO | ENTL_DEVICE_FLAG_SIGNAL ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
	}
	else if( result == ENTL_ACTION_SIG_ERR ) {  // request for signal as error flag is set
		dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer		
	}
	else {
		if( result & ENTL_ACTION_PROC_AIT ) {
	    	// AIT message is received, put in to the receive buffer
	    	struct entt_ioctl_ait_data *ait_data ;
	    	unsigned int len = skb->len ;
	    	char *cp = skb->data + sizeof(struct ethhdr) ;
			//ENTL_DEBUG("entl_device_process_rx_packet %02x %02x %02x %02x %02x %02x %02x %02x \n", cp[0], cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7] );
	    	
	    	ait_data = kzalloc( sizeof(struct entt_ioctl_ait_data), GFP_ATOMIC );
			//ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got skb len %d\n", dev->name, len );
	    	if( len > sizeof(struct ethhdr) ) {
	    		unsigned char *data = skb->data + sizeof(struct ethhdr) ;
	    		memcpy( &ait_data->message_len, data, sizeof(u32)) ;
	    		if( ait_data->message_len && ait_data->message_len < MAX_AIT_MESSAGE_SIZE ) 
	    		{
					//ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got message_len %d\n", dev->name, ait_data->message_len );
	    			memcpy( ait_data->data, data + sizeof(u32), ait_data->message_len ) ;
	    		}
	    		else {
					ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got too big message_len %d\n", adapter->netdev->name, ait_data->message_len );
	    			ait_data->message_len = 0 ;
	    		}
	    	}
	    	entl_new_AIT_message( &dev->stm, ait_data ) ;
			//ENTL_DEBUG("ENTL %s entl_device_process_rx_packet got ATI len %d\n", dev->name, ait_data->message_len );
		}
		if( result & ENTL_ACTION_SIG_AIT ) {
			dev->flag |= ENTL_DEVICE_FLAG_SIGNAL2 ;
		}
	    if( result & ENTL_ACTION_SEND ) {
	    	int ret ;
	    	// need to send message

	    	// SEND_DAT flag is set on SEND state to check if TX queue has data
	    	if( result & ENTL_ACTION_SEND_DAT &&  ENTL_skb_queue_has_data( &dev->tx_skb_queue ) ) {
	    		// TX queue has data, so transfer with data
				struct sk_buff *dt = pop_front_ENTL_skb_queue( &dev->tx_skb_queue );
    			while( NULL != dt && skb_is_gso(dt) ) {  // GSO can't be used for ENTL 
					ENTL_DEBUG("ENTL %s entl_device_process_rx_packet emit gso packet\n", adapter->netdev->name );    				
					e1000_xmit_frame( dt, adapter->netdev ) ;
					dt = pop_front_ENTL_skb_queue( &dev->tx_skb_queue );
    			}
	    		if( dt ) {
	    			ENTL_DEBUG("ENTL %s entl_device_process_rx_packet emit packet len %d count %d head %d tail %d\n", adapter->netdev->name , dt->len, dev->tx_skb_queue.count, dev->tx_skb_queue.head, dev->tx_skb_queue.tail );    				
					e1000_xmit_frame( dt, adapter->netdev ) ;
	    		}
	    		else {
	    			// tx queue becomes empty, so inject a new packet
	    			ret = entl_next_send( &dev->stm, &d_u_addr, &d_l_addr ) ;
			    	if( (d_u_addr & (u16)ENTL_MESSAGE_MASK) != ENTL_MESSAGE_NOP_U ) {  // last minute check
			    		unsigned long flags ;
						spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
			    		result = inject_message( dev, d_u_addr, d_l_addr, ret ) ;
			    		spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
			    		// if failed to inject message, so invoke the task
			    		if( result == 1 ) {
			    			// resource error, so retry
			    			dev->u_addr = d_u_addr ;
			    			dev->l_addr = d_l_addr ;
			    			dev->action = ret ;
			    			dev->flag |= ENTL_DEVICE_FLAG_RETRY ;
							mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
			    		}
			    		else if( result == -1 ) {
			     			entl_state_error( &dev->stm, ENTL_ERROR_FATAL ) ;
			   				dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
							mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
			    		}
			    		else {
			    			// clear watchdog flag
							dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;
			    		}
			    	}			   	
	    		}
	    		// netif queue handling for flow control
	    		if( dev->queue_stopped && ENTL_skb_queue_unused( &dev->tx_skb_queue ) > 2 ) {
					netif_start_queue(adapter->netdev);
					dev->queue_stopped = 0 ;
				}
	    	}
	    	else {
		    	ret = entl_next_send( &dev->stm, &d_u_addr, &d_l_addr ) ;
		    	if( (d_u_addr & (u16)ENTL_MESSAGE_MASK) != ENTL_MESSAGE_NOP_U ) {  // last minute check
		    		unsigned long flags ;
					spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
		    		result = inject_message( dev, d_u_addr, d_l_addr, ret ) ;
		    		spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
		    		// if failed to inject message, so invoke the task
		    		if( result == 1 ) {
		    			// resource error, so retry
		    			dev->u_addr = d_u_addr ;
		    			dev->l_addr = d_l_addr ;
		    			dev->action = ret ;
		    			dev->flag |= ENTL_DEVICE_FLAG_RETRY ;
						mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
		    		}
		    		else if( result == -1 ) {
		     			entl_state_error( &dev->stm, ENTL_ERROR_FATAL ) ;
		   				dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
						mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
		    		}
		    		else {
		    			// clear watchdog flag
						dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;
		    		}
		    	}			    		
	    	}

	    }

    }

	return retval ;

}

// process packet being sent. The ENTL message can only be ent over the single (non MSS) packet
//  Assuming this is called from non-interrupt context
static void entl_device_process_tx_packet( entl_device_t *dev, struct sk_buff *skb )
{
	u16 u_addr;
	u32 l_addr;
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
		ENTL_DEBUG("ENTL %s entl_device_process_tx_packet got a gso packet\n", dev->name );
	}
	else {
	    int ret = entl_next_send_tx( &dev->stm, &u_addr, &l_addr ) ;
	    if( ret & ENTL_ACTION_SIG_AIT ) {
			dev->flag |= ENTL_DEVICE_FLAG_SIGNAL2 ;  // AIT send completion signal
		}
		d_addr[0] = (u_addr >> 8) ; 
		d_addr[1] = u_addr ;
		d_addr[2] = l_addr >> 24 ;
		d_addr[3] = l_addr >> 16;
		d_addr[4] = l_addr >> 8;
		d_addr[5] = l_addr ;		
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
		if( u_addr != ENTL_MESSAGE_NOP_U ) {
			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;			
		}
		ENTL_DEBUG("ENTL %s entl_device_process_tx_packet got a single packet with %04x %08x t:%04x\n", dev->name, u_addr, l_addr, eth->h_proto );
	}

}

/**
 * entl_e1000e_set_rx_mode - ENTL versin, always set Promiscuous mode
 * @netdev: network interface device structure
 *
 * The ndo_set_rx_mode entry point is called whenever the unicast or multicast
 * address list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast,
 * promiscuous mode, and all-multi behavior.
 **/
static void entl_e1000e_set_rx_mode(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (pm_runtime_suspended(netdev->dev.parent))
		return;

	/* Check for Promiscuous and All Multicast modes */
	rctl = er32(RCTL);                                           

	rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);           // ENTL: We need to set this
#ifdef HAVE_VLAN_RX_REGISTER
	rctl &= ~E1000_RCTL_VFE;
#else
	/* Do not hardware filter VLANs in promisc mode */
	e1000e_vlan_filter_disable(adapter);
#endif /* HAVE_VLAN_RX_REGISTER */

    ENTL_DEBUG("entl_e1000e_set_rx_mode  RCTL = %08x\n", rctl );

	ew32(RCTL, rctl);
#ifndef HAVE_VLAN_RX_REGISTER

#ifdef NETIF_F_HW_VLAN_CTAG_RX
	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
#else
	if (netdev->features & NETIF_F_HW_VLAN_RX)
#endif
		e1000e_vlan_strip_enable(adapter);
	else
		e1000e_vlan_strip_disable(adapter);
#endif /* HAVE_VLAN_RX_REGISTER */
}

/**
 * entl_e1000_setup_rctl - ENTL version of configure the receive control registers
 * @adapter: Board private structure
 **/
static void entl_e1000_setup_rctl(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl, rfctl;
	u32 pages = 0;

	/* Workaround Si errata on PCHx - configure jumbo frame flow.
	 * If jumbo frames not set, program related MAC/PHY registers
	 * to h/w defaults
	 */
	if (hw->mac.type >= e1000_pch2lan) {
		s32 ret_val;

		if (adapter->netdev->mtu > ETH_DATA_LEN)
			ret_val = e1000_lv_jumbo_workaround_ich8lan(hw, true);
		else
			ret_val = e1000_lv_jumbo_workaround_ich8lan(hw, false);

		if (ret_val)
			e_dbg("failed to enable|disable jumbo frame workaround mode\n");
	}

	/* Program MC offset vector base */
	rctl = er32(RCTL);
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
	    E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
	    (adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/* Do not Store bad packets */
	rctl &= ~E1000_RCTL_SBP;

	/* Enable Long Packet receive */
	if (adapter->netdev->mtu <= ETH_DATA_LEN) {
		ENTL_DEBUG("entl_e1000_setup_rctl %d <= %d\n", adapter->netdev->mtu, ETH_DATA_LEN );
		rctl &= ~E1000_RCTL_LPE;
	}
	else {
		ENTL_DEBUG("entl_e1000_setup_rctl %d > %d\n", adapter->netdev->mtu, ETH_DATA_LEN );
		rctl |= E1000_RCTL_LPE;
	}

	/* Some systems expect that the CRC is included in SMBUS traffic. The
	 * hardware strips the CRC before sending to both SMBUS (BMC) and to
	 * host memory when this is enabled
	 */
	if (adapter->flags2 & FLAG2_CRC_STRIPPING)
		rctl |= E1000_RCTL_SECRC;

	/* Workaround Si errata on 82577/82578 - configure IPG for jumbos */
	if ((hw->mac.type == e1000_pchlan) && (rctl & E1000_RCTL_LPE)) {
		u32 mac_data;
		u16 phy_data;

		ENTL_DEBUG("entl_e1000_setup_rctl Workaround Si errata on 82577/82578 - configure IPG for jumbos\n" );

		e1e_rphy(hw, PHY_REG(770, 26), &phy_data);
		phy_data &= 0xfff8;
		phy_data |= (1 << 2);
		e1e_wphy(hw, PHY_REG(770, 26), phy_data);

		mac_data = er32(FFLT_DBG);
		mac_data |= (1 << 17);
		ew32(FFLT_DBG, mac_data);

		if (hw->phy.type == e1000_phy_82577) {
			e1e_rphy(hw, 22, &phy_data);
			phy_data &= 0x0fff;
			phy_data |= (1 << 14);
			e1e_wphy(hw, 0x10, 0x2823);
			e1e_wphy(hw, 0x11, 0x0003);
			e1e_wphy(hw, 22, phy_data);
		}
	}

	/* Setup buffer sizes */
	rctl &= ~E1000_RCTL_SZ_4096;
	rctl |= E1000_RCTL_BSEX;
	switch (adapter->rx_buffer_len) {
	case 2048:
	default:
		ENTL_DEBUG("entl_e1000_setup_rctl E1000_RCTL_SZ_2048\n" );
		rctl |= E1000_RCTL_SZ_2048;
		rctl &= ~E1000_RCTL_BSEX;
		break;
	case 4096:
		ENTL_DEBUG("entl_e1000_setup_rctl E1000_RCTL_SZ_4096\n" );
		rctl |= E1000_RCTL_SZ_4096;
		break;
	case 8192:
		ENTL_DEBUG("entl_e1000_setup_rctl E1000_RCTL_SZ_8192\n" );
		rctl |= E1000_RCTL_SZ_8192;
		break;
	case 16384:
		ENTL_DEBUG("entl_e1000_setup_rctl E1000_RCTL_SZ_16384\n" );
		rctl |= E1000_RCTL_SZ_16384;
		break;
	}

	/* Enable Extended Status in all Receive Descriptors */
	rfctl = er32(RFCTL);
	rfctl |= E1000_RFCTL_EXTEN;
	ew32(RFCTL, rfctl);

	/* 82571 and greater support packet-split where the protocol
	 * header is placed in skb->data and the packet data is
	 * placed in pages hanging off of skb_shinfo(skb)->nr_frags.
	 * In the case of a non-split, skb->data is linearly filled,
	 * followed by the page buffers.  Therefore, skb->data is
	 * sized to hold the largest protocol header.
	 *
	 * allocations using alloc_page take too long for regular MTU
	 * so only enable packet split for jumbo frames
	 *
	 * Using pages when the page size is greater than 16k wastes
	 * a lot of memory, since we allocate 3 pages at all times
	 * per packet.
	 */
	pages = PAGE_USE_COUNT(adapter->netdev->mtu);
	if ((pages <= 3) && (PAGE_SIZE <= 16384) && (rctl & E1000_RCTL_LPE))
		adapter->rx_ps_pages = pages;
	else
		adapter->rx_ps_pages = 0;

	ENTL_DEBUG("entl_e1000_setup_rctl rx_ps_pages = %d\n", adapter->rx_ps_pages );

	if (adapter->rx_ps_pages) {
		u32 psrctl = 0;

		/* Enable Packet split descriptors */
		rctl |= E1000_RCTL_DTYP_PS;

		psrctl |= adapter->rx_ps_bsize0 >> E1000_PSRCTL_BSIZE0_SHIFT;

		switch (adapter->rx_ps_pages) {
		case 3:
			psrctl |= PAGE_SIZE << E1000_PSRCTL_BSIZE3_SHIFT;
			/* fall-through */
		case 2:
			psrctl |= PAGE_SIZE << E1000_PSRCTL_BSIZE2_SHIFT;
			/* fall-through */
		case 1:
			psrctl |= PAGE_SIZE >> E1000_PSRCTL_BSIZE1_SHIFT;
			break;
		}

		ew32(PSRCTL, psrctl);
	}

	/* This is useful for sniffing bad packets. */
	if (adapter->netdev->features & NETIF_F_RXALL) {
		/* UPE and MPE will be handled by normal PROMISC logic
		 * in e1000e_set_rx_mode
		 */
		rctl |= (E1000_RCTL_SBP |	/* Receive bad packets */
			 E1000_RCTL_BAM |	/* RX All Bcast Pkts */
			 E1000_RCTL_PMCF);	/* RX All MAC Ctrl Pkts */

		rctl &= ~(E1000_RCTL_VFE |	/* Disable VLAN filter */
			  E1000_RCTL_DPF |	/* Allow filtered pause */
			  E1000_RCTL_CFIEN);	/* Dis VLAN CFIEN Filter */
		/* Do not mess with E1000_CTRL_VME, it affects transmit as well,
		 * and that breaks VLANs.
		 */
	}
    ENTL_DEBUG("entl_e1000_setup_rctl  RCTL = %08x\n", rctl );

	ew32(RCTL, rctl);
	/* just started the receive unit, no need to restart */
	adapter->flags &= ~FLAG_RESTART_NOW;
}

/**
 * entl_e1000_configure_rx - ENTL version of Configure Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void entl_e1000_configure_rx(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *rx_ring = adapter->rx_ring;
	u64 rdba;
	u32 rdlen, rctl, rxcsum, ctrl_ext;

	if (adapter->rx_ps_pages) {
		/* this is a 32 byte descriptor */
		rdlen = rx_ring->count *
		    sizeof(union e1000_rx_desc_packet_split);
		adapter->clean_rx = e1000_clean_rx_irq_ps;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers_ps;
		ENTL_DEBUG("entl_e1000_configure_rx use e1000_alloc_rx_buffers_ps\n" );
#ifdef CONFIG_E1000E_NAPI
	} else if (adapter->netdev->mtu > ETH_FRAME_LEN + ETH_FCS_LEN) {
		rdlen = rx_ring->count * sizeof(union e1000_rx_desc_extended);
		adapter->clean_rx = e1000_clean_jumbo_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_jumbo_rx_buffers;
		ENTL_DEBUG("entl_e1000_configure_rx use e1000_alloc_jumbo_rx_buffers\n" );
#endif
	} else {
		rdlen = rx_ring->count * sizeof(union e1000_rx_desc_extended);
		adapter->clean_rx = e1000_clean_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers;
		ENTL_DEBUG("entl_e1000_configure_rx use e1000_alloc_rx_buffers\n" );
	}

	/* disable receives while setting up the descriptors */
	rctl = er32(RCTL);
	if (!(adapter->flags2 & FLAG2_NO_DISABLE_RX))
		ew32(RCTL, rctl & ~E1000_RCTL_EN);
	e1e_flush();
	usleep_range(10000, 20000);

	if (adapter->flags2 & FLAG2_DMA_BURST) {
		ENTL_DEBUG("entl_e1000_configure_rx set DMA burst\n" );
		/* set the writeback threshold (only takes effect if the RDTR
		 * is set). set GRAN=1 and write back up to 0x4 worth, and
		 * enable prefetching of 0x20 Rx descriptors
		 * granularity = 01
		 * wthresh = 04,
		 * hthresh = 04,
		 * pthresh = 0x20
		 */
		ew32(RXDCTL(0), E1000_RXDCTL_DMA_BURST_ENABLE);
		ew32(RXDCTL(1), E1000_RXDCTL_DMA_BURST_ENABLE);

		/* override the delay timers for enabling bursting, only if
		 * the value was not set by the user via module options
		 */
		if (adapter->rx_int_delay == DEFAULT_RDTR)
			adapter->rx_int_delay = BURST_RDTR;
		if (adapter->rx_abs_int_delay == DEFAULT_RADV)
			adapter->rx_abs_int_delay = BURST_RADV;
	}

	/* set the Receive Delay Timer Register */
	ENTL_DEBUG("entl_e1000_configure_rx set Receive Delay Timer Register = %d\n", adapter->rx_int_delay );
	ew32(RDTR, adapter->rx_int_delay);

	/* irq moderation */
	ENTL_DEBUG("entl_e1000_configure_rx set Abs Delay Timer Register = %d\n", adapter->rx_abs_int_delay );
	ew32(RADV, adapter->rx_abs_int_delay);
	if ((adapter->itr_setting != 0) && (adapter->itr != 0))
		e1000e_write_itr(adapter, adapter->itr);

	ctrl_ext = er32(CTRL_EXT);
#ifdef CONFIG_E1000E_NAPI
	/* Auto-Mask interrupts upon ICR access */
	ctrl_ext |= E1000_CTRL_EXT_IAME;
	ew32(IAM, 0xffffffff);
#endif
	ew32(CTRL_EXT, ctrl_ext);
	e1e_flush();

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	rdba = rx_ring->dma;
	ew32(RDBAL(0), (rdba & DMA_BIT_MASK(32)));
	ew32(RDBAH(0), (rdba >> 32));
	ew32(RDLEN(0), rdlen);
	ew32(RDH(0), 0);
	ew32(RDT(0), 0);
	rx_ring->head = adapter->hw.hw_addr + E1000_RDH(0);
	rx_ring->tail = adapter->hw.hw_addr + E1000_RDT(0);

	/* Enable Receive Checksum Offload for TCP and UDP */
	rxcsum = er32(RXCSUM);
#ifdef HAVE_NDO_SET_FEATURES
	if (adapter->netdev->features & NETIF_F_RXCSUM)
#else
	if (adapter->flags & FLAG_RX_CSUM_ENABLED)
#endif
		rxcsum |= E1000_RXCSUM_TUOFL;
	else
		rxcsum &= ~E1000_RXCSUM_TUOFL;
	ew32(RXCSUM, rxcsum);

	/* With jumbo frames, excessive C-state transition latencies result
	 * in dropped transactions.
	 */
	if (adapter->netdev->mtu > ETH_DATA_LEN) {
		u32 lat =
		    ((er32(PBA) & E1000_PBA_RXA_MASK) * 1024 -
		     adapter->max_frame_size) * 8 / 1000;

		ENTL_DEBUG("entl_e1000_configure_rx adapter->netdev->mtu %d > ETH_DATA_LEN %d lat = %d\n", adapter->netdev->mtu, ETH_DATA_LEN, lat );

		if (adapter->flags & FLAG_IS_ICH) {
			u32 rxdctl = er32(RXDCTL(0));

			ew32(RXDCTL(0), rxdctl | 0x3);
		}
#ifdef HAVE_PM_QOS_REQUEST_LIST_NEW
		pm_qos_update_request(&adapter->pm_qos_req, lat);
#elif defined(HAVE_PM_QOS_REQUEST_LIST)
		pm_qos_update_request(&adapter->pm_qos_req, lat);
#else
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
					  adapter->netdev->name, lat);
#endif
	} else {
		ENTL_DEBUG("entl_e1000_configure_rx adapter->netdev->mtu %d <= ETH_DATA_LEN %d default qos = %d\n", adapter->netdev->mtu, ETH_DATA_LEN, PM_QOS_DEFAULT_VALUE );

#ifdef HAVE_PM_QOS_REQUEST_LIST_NEW
		pm_qos_update_request(&adapter->pm_qos_req,
				      PM_QOS_DEFAULT_VALUE);
#elif defined(HAVE_PM_QOS_REQUEST_LIST)
		pm_qos_update_request(&adapter->pm_qos_req,
				      PM_QOS_DEFAULT_VALUE);
#else
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
					  adapter->netdev->name,
					  PM_QOS_DEFAULT_VALUE);
#endif
	}
	ENTL_DEBUG("entl_e1000_configure_rx  RCTL = %08x\n", rctl );

	/* Enable Receives */
	ew32(RCTL, rctl);
}



/// entl version of e1000_configure - configure the hardware for Rx and Tx

static void entl_e1000_configure(struct e1000_adapter *adapter)
{
	struct e1000_ring *rx_ring = adapter->rx_ring;
	entl_device_t *dev = &adapter->entl_dev ;
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;

	ENTL_DEBUG("entl_e1000_configure is called\n" );

	entl_e1000e_set_rx_mode(adapter->netdev);

#if defined(NETIF_F_HW_VLAN_TX) || defined(NETIF_F_HW_VLAN_CTAG_TX)
	e1000_restore_vlan(adapter);
#endif
	e1000_init_manageability_pt(adapter);

	// We don’t need immediate interrupt on Tx completion. (unless buffer was full and quick responce is required, but it’s not likely)
	e1000_configure_tx(adapter);

#ifdef NETIF_F_RXHASH
	if (adapter->netdev->features & NETIF_F_RXHASH)
		e1000e_setup_rss_hash(adapter);
#endif
	// my version of setup, with debug output
	entl_e1000_setup_rctl(adapter);
	// my version of configure rx, wth debug output
	entl_e1000_configure_rx(adapter);
	adapter->alloc_rx_buf(rx_ring, e1000_desc_unused(rx_ring), GFP_KERNEL);

	// initialize the state machine
	entl_state_machine_init( &dev->stm ) ;
	strlcpy(dev->stm.name, dev->name, sizeof(dev->stm.name));
	
	// AK: Setting MAC address for Hello handling
	entl_e1000_set_my_addr( &adapter->entl_dev, netdev->dev_addr ) ;

	// force to check the link status on kernel task
	hw->mac.get_link_status = true;
}

static void entl_e1000_set_my_addr( entl_device_t *dev, const u8 *addr ) 
{
	u16 u_addr;
	u32 l_addr;

	ENTL_DEBUG("entl_e1000_set_my_addr set %d.%d.%d.%d.%d.%d\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5] );

		
    u_addr = (u16)addr[0] << 8 | addr[1] ;
    l_addr = (u32)addr[2] << 24 | (u32)addr[3] << 16 | (u32)addr[4] << 8 | (u32)addr[5] ;
    entl_set_my_adder( &dev->stm, u_addr, l_addr ) ;

}

static void init_ENTL_skb_queue( ENTL_skb_queue_t* q ) 
{
    q->size = E1000_DEFAULT_TXD ;
    q->count = 0 ;
    q->head = q->tail = 0 ;
}

static int ENTL_skb_queue_full( ENTL_skb_queue_t* q ) 
{
    if( q->size == q->count ) return 1 ;
    return 0 ;
}

static int ENTL_skb_queue_has_data( ENTL_skb_queue_t* q ) 
{
    return q->count ;
}

static int ENTL_skb_queue_unused( ENTL_skb_queue_t* q ) 
{
    return q->size - q->count - 1 ;
}

static int push_back_ENTL_skb_queue(ENTL_skb_queue_t* q, struct sk_buff *dt ) 
{
    if( q->size == q->count ) {
    	// queue full
    	return -1 ;
    }
    q->data[q->tail] = dt ;
    q->tail = (q->tail+1) % q->size ;
    q->count++ ;
    return q->size - q->count ;
}

static struct sk_buff *front_ENTL_skb_queue(ENTL_skb_queue_t* q ) 
{
	struct sk_buff *dt ;
	if( q->count == 0 ) return NULL ; // queue empty
	dt = q->data[q->head] ;
    return dt ;
}

static struct sk_buff *pop_front_ENTL_skb_queue(ENTL_skb_queue_t* q ) 
{
	struct sk_buff *dt ;
	if( q->count == 0 ) return NULL ; // queue empty
	dt = q->data[q->head] ;
    q->head = (q->head+1) % q->size ;
    q->count-- ;
    return dt ;
}

/// tx queue handling, replacing e1000_xmit_frame
static netdev_tx_t entl_tx_transmit( struct sk_buff *skb, struct net_device *netdev ) 
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	entl_device_t *dev = &adapter->entl_dev ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;

	if( ENTL_skb_queue_full( &dev->tx_skb_queue ) ) {
		ENTL_DEBUG("entl_tx_transmit Queue full!! %d %d\n", dev->tx_skb_queue.count,  dev->tx_skb_queue.size ) ;
		BUG_ON( dev->tx_skb_queue.count >= dev->tx_skb_queue.size) ;
		return NETDEV_TX_BUSY;
	}
	if( eth->h_proto != ETH_P_ECLP && eth->h_proto != ETH_P_ECLD ) {
		ENTL_DEBUG("%s entl_tx_transmit dropping non EC type %4x %p len %d d: %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x  %02x%02x %02x %02x %02x %02x %02x %02x\n", netdev->name, eth->h_proto, skb, skb->len,
		  skb->data[0], skb->data[1], skb->data[2], skb->data[3], skb->data[4], skb->data[5], 
		  skb->data[6], skb->data[7], skb->data[8], skb->data[9], skb->data[10], skb->data[11], 
		  skb->data[12], skb->data[13],
		  skb->data[14], skb->data[15], skb->data[16], skb->data[17], skb->data[18], skb->data[19]
		  ) ;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	push_back_ENTL_skb_queue( &dev->tx_skb_queue, skb ) ;

	ENTL_DEBUG("%s entl_tx_transmit got packet %p len %d count %d head %d tail %d d: %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x  %02x%02x %02x %02x %02x %02x %02x %02x\n", netdev->name, skb, skb->len, dev->tx_skb_queue.count, dev->tx_skb_queue.head, dev->tx_skb_queue.tail,
	  skb->data[0], skb->data[1], skb->data[2], skb->data[3], skb->data[4], skb->data[5], 
	  skb->data[6], skb->data[7], skb->data[8], skb->data[9], skb->data[10], skb->data[11], 
	  skb->data[12], skb->data[13],
	  skb->data[14], skb->data[15], skb->data[16], skb->data[17], skb->data[18], skb->data[19]
	  ) ;

	if( ENTL_skb_queue_unused( &dev->tx_skb_queue ) < 2 ) {
		ENTL_DEBUG("entl_tx_transmit Queue near full, flow control %d %d\n", dev->tx_skb_queue.count,  dev->tx_skb_queue.size ) ;
		netif_stop_queue(netdev);
		dev->queue_stopped = 1 ;
		return NETDEV_TX_BUSY;		
	}
	return NETDEV_TX_OK;

}


