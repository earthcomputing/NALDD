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
	struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
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
		int i ;
		struct ethhdr *eth = (struct ethhdr *)skb->data ;
		skb->len = ETH_ZLEN + ETH_FCS_LEN ;     // min packet size + crc
		memcpy(eth->h_source, netdev->dev_addr, ETH_ALEN);
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
		eth->h_proto = 0 ; // protocol type is not used anyway
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
	if( dev->flag & ENTL_DEVICE_FLAG_HELLO ) {
		int t ;
		struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
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
		if( dev->stm.current_state.current_state == ENTL_STATE_HELLO ) {
			__u16 u_addr ;
			__u32 l_addr ;
			if( entl_get_hello(&dev->stm, &u_addr, &l_addr) ){
				unsigned long flags;
				int result ;
				// attomically check to make sure we still need to send hello
				spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
	    		result = inject_message( dev, u_addr, l_addr ) ;
	    		spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
	    		if( result == 0 ) {
	    			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_HELLO ;
					ENTL_DEBUG("ENTL %s entl_watchdog_task hello packet sent\n", dev->name );
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
			 ENTL_DEBUG("ENTL %s entl_watchdog_task not hello state but %d\n", dev->stm.current_state.current_state );
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
	    result = inject_message( dev, dev->u_addr, dev->l_addr ) ;
	    spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
	    if( result == 0 ) {
    		dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_RETRY ;
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry packet sent\n", dev->name );
	    }
	    else {
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry packet failed with %d \n", dev->name, result );	    			
	    }
	}
	else if( dev->flag & ENTL_DEVICE_FLAG_WAITING )
	{
		if( entl_retry_hello(&dev->stm ) ) {
			dev->flag &= ~(__u32)ENTL_DEVICE_FLAG_WAITING ;
			dev->flag |= ENTL_DEVICE_FLAG_HELLO ;
			ENTL_DEBUG("ENTL %s entl_watchdog_task retry hello sending\n", dev->name );
		}
	}
	restart_watchdog:
	mod_timer(&dev->watchdog_timer, round_jiffies(jiffies + wakeup));
}

static void entl_device_init( entl_device_t *dev ) 
{
	struct e1000_adapter *adapter = container_of( dev, struct e1000_adapter, entl_dev );
	struct net_device *netdev = adapter->netdev;

	if ( netdev ) {
		strlcpy(dev->name, netdev->name, sizeof(dev->name));
	}
	else {
		strlcpy(dev->name, "unknown", sizeof(dev->name));
	}

	// initialize the state machine
	entl_state_machine_init( &dev->stm ) ;
	strlcpy(dev->stm.name, dev->name, sizeof(dev->stm.name));


	dev->user_pid = 0 ;
	dev->flag = 0 ;

  	spin_lock_init( &adapter->tx_ring_lock ) ;

	// watchdog timer & task setup
	init_timer(&dev->watchdog_timer);
	dev->watchdog_timer.function = entl_watchdog;
	dev->watchdog_timer.data = (unsigned long)dev;
	INIT_WORK(&dev->watchdog_task, entl_watchdog_task);
	ENTL_DEBUG("ENTL %s entl_device_init done\n", dev->name );

}

static void entl_device_link_up( entl_device_t *dev ) 
{
	entl_link_up( &dev->stm ) ;
	if( dev->stm.current_state.current_state ==  ENTL_STATE_HELLO) {
		dev->flag |= ENTL_DEVICE_FLAG_SIGNAL | ENTL_DEVICE_FLAG_HELLO ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
	}
}

static void entl_device_link_down( entl_device_t *dev ) 
{
	entl_state_error( &dev->stm, ENTL_ERROR_FLAG_LINKDONW ) ;
	dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
	mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer	
}

static int entl_do_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd) 
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	entl_device_t *dev = &adapter->entl_dev ;
  	struct entl_ioctl_data entl_data ;

	switch( cmd )
	{
	case SIOCDEVPRIVATE_ENTL_RD_CURRENT:
		ENTL_DEBUG("ENTL %s ioctl reading current state\n", dev->name );
		entl_data.link_state = test_bit(__E1000_DOWN, &adapter->state) ? 0 : 1 ;
		entl_read_current_state( &dev->stm, &entl_data.state ) ;
		entl_read_error_state( &dev->stm, &entl_data.error_state ) ;
		copy_to_user(ifr->ifr_data, &entl_data, sizeof(struct entl_ioctl_data));
		break;		
	case SIOCDEVPRIVATE_ENTL_RD_ERROR:
		ENTL_DEBUG("ENTL %s ioctl reading error state\n", dev->name );
		entl_data.link_state = test_bit(__E1000_DOWN, &adapter->state) ? 0 : 1 ;
		entl_read_error_state( &dev->stm, &entl_data.state ) ;
		entl_read_error_state( &dev->stm, &entl_data.error_state ) ;
		copy_to_user(ifr->ifr_data, &entl_data, sizeof(struct entl_ioctl_data));
		break;
	case SIOCDEVPRIVATE_ENTL_SET_SIGRCVR:
		copy_from_user(&entl_data, ifr->ifr_data, sizeof(struct entl_ioctl_data) ) ;
		ENTL_DEBUG("ENTL %s ioctl user_pid %d is set\n", dev->name, entl_data.pid );
		dev->user_pid = entl_data.pid ;
		break;
	case SIOCDEVPRIVATE_ENTL_GEN_SIGNAL:
		ENTL_DEBUG("ENTL %s ioctl generating signal to user\n", dev->name );
		dev->flag |= ENTL_DEVICE_FLAG_SIGNAL ;
		mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer		
		break ;		
	case SIOCDEVPRIVATE_ENTL_DO_INIT:
		ENTL_DEBUG("ENTL %s ioctl initialize the device\n", dev->name );
		entl_e1000_configure( adapter ) ;
		break ;
	default:
		ENTL_DEBUG("ENTL %s ioctl error: undefined cmd %d\n", dev->name, cmd);
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
	bool ret = true ;
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

    if( d_u_addr & ENTL_MESSAGE_ONLY_U ) ret = false ; // this is message only packet

    result = entl_received( &dev->stm, s_u_addr, s_l_addr, d_u_addr, d_l_addr ) ;

    if( result == 1 ) {
    	// need to send message
    	// this is ISR version to get the next to send
    	entl_next_send( &dev->stm, &d_u_addr, &d_l_addr ) ;
    	if( (d_u_addr & (u16)ENTL_MESSAGE_MASK) != ENTL_MESSAGE_NOP_U ) {  // last minute check
    		unsigned long flags ;
			spin_lock_irqsave( &adapter->tx_ring_lock, flags ) ;
    		result = inject_message( dev, d_u_addr, d_l_addr ) ;
    		spin_unlock_irqrestore( &adapter->tx_ring_lock, flags ) ;
    		// if failed to inject message, so invoke the task
    		if( result == 1 ) {
    			// resource error, so retry
    			dev->u_addr = d_u_addr ;
    			dev->l_addr = d_l_addr ;
    			dev->flag |= ENTL_DEVICE_FLAG_RETRY ;
				mod_timer( &dev->watchdog_timer, jiffies + 1 ) ; // trigger timer
    		}
    		else if( result == -1 ) {
     			entl_state_error( &dev->stm, ENTL_ERROR_FATAL ) ;
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
	}
	else {
		entl_next_send( &dev->stm, &u_addr, &l_addr ) ;
		d_addr[0] = (u_addr >> 8) ; 
		d_addr[1] = u_addr ;
		d_addr[2] = l_addr >> 24 ;
		d_addr[3] = l_addr >> 16;
		d_addr[4] = l_addr >> 8;
		d_addr[5] = l_addr ;		
		memcpy(eth->h_dest, d_addr, ETH_ALEN);
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
	/* Do not hardware filter VLANs in promisc mode */

	// Avoid buggy code that does read-modify-write on rctl
	// e1000e_vlan_filter_disable(adapter);
	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER) {
		/* disable VLAN receive filtering */
		rctl &= ~(E1000_RCTL_VFE | E1000_RCTL_CFIEN);

		// When VFE is not set, there's no need to set VFTA array
		//if (adapter->mng_vlan_id != (u16)E1000_MNG_VLAN_NONE) {
		//	e1000_vlan_rx_kill_vid(netdev, htons(ETH_P_8021Q),
		//			       adapter->mng_vlan_id);
			adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
		//}
	}
    
	ew32(RCTL, rctl);

	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX)  // this flag is set on prove function
		e1000e_vlan_strip_enable(adapter);           // we don't use VLAN, so should not matter                                  
	else
		e1000e_vlan_strip_disable(adapter);
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

	/* Do Store bad packets */
	rctl |= E1000_RCTL_SBP;

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
	if (adapter->flags2 & FLAG2_CRC_STRIPPING)           // this bit is not set
		rctl |= E1000_RCTL_SECRC;

	/* Workaround Si errata on 82577 PHY - configure IPG for jumbos */
	if ((hw->phy.type == e1000_phy_82577) && (rctl & E1000_RCTL_LPE)) {
		u16 phy_data;

		ENTL_DEBUG("entl_e1000_setup_rctl Workaround Si errata on 82577 PHY - configure IPG for jumbos\n" );
		e1e_rphy(hw, PHY_REG(770, 26), &phy_data);
		phy_data &= 0xfff8;
		phy_data |= (1 << 2);
		e1e_wphy(hw, PHY_REG(770, 26), phy_data);

		e1e_rphy(hw, 22, &phy_data);
		phy_data &= 0x0fff;
		phy_data |= (1 << 14);
		e1e_wphy(hw, 0x10, 0x2823);
		e1e_wphy(hw, 0x11, 0x0003);
		e1e_wphy(hw, 22, phy_data);
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
	// ENTL always get all packets
	//if (adapter->netdev->features & NETIF_F_RXALL) {
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
	//}

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

	} else if (adapter->netdev->mtu > ETH_FRAME_LEN + ETH_FCS_LEN) {
		rdlen = rx_ring->count * sizeof(union e1000_rx_desc_extended);
		adapter->clean_rx = e1000_clean_jumbo_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_jumbo_rx_buffers;
		ENTL_DEBUG("entl_e1000_configure_rx use e1000_alloc_jumbo_rx_buffers\n" );
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

	if (adapter->flags2 & FLAG2_DMA_BURST) {   // this bit is set
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

	ENTL_DEBUG("entl_e1000_configure_rx set Receive Delay Timer Register = %d\n", adapter->rx_int_delay );
	/* set the Receive Delay Timer Register */
	ew32(RDTR, adapter->rx_int_delay);


	ENTL_DEBUG("entl_e1000_configure_rx set Abs Delay Timer Register = %d\n", adapter->rx_abs_int_delay );
	/* irq moderation */
	ew32(RADV, adapter->rx_abs_int_delay);
	if ((adapter->itr_setting != 0) && (adapter->itr != 0))
		e1000e_write_itr(adapter, adapter->itr);

	ctrl_ext = er32(CTRL_EXT);
	/* Auto-Mask interrupts upon ICR access */
	ctrl_ext |= E1000_CTRL_EXT_IAME;
	ew32(IAM, 0xffffffff);
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

	writel(0, rx_ring->head);
	if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)      // this bit is not set
		e1000e_update_rdt_wa(rx_ring, 0);
	else
		writel(0, rx_ring->tail);

	/* Enable Receive Checksum Offload for TCP and UDP */
	rxcsum = er32(RXCSUM);
	if (adapter->netdev->features & NETIF_F_RXCSUM)
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

		pm_qos_update_request(&adapter->pm_qos_req, lat);
	} else {
		ENTL_DEBUG("entl_e1000_configure_rx adapter->netdev->mtu %d <= ETH_DATA_LEN %d default qos = %d\n", adapter->netdev->mtu, ETH_DATA_LEN, PM_QOS_DEFAULT_VALUE );
		pm_qos_update_request(&adapter->pm_qos_req,
				      PM_QOS_DEFAULT_VALUE);
	}

	/* Enable Receives */
	ew32(RCTL, rctl);
}

/// entl version of e1000_configure - configure the hardware for Rx and Tx
static void entl_e1000_configure(struct e1000_adapter *adapter) 
{
	struct e1000_ring *rx_ring = adapter->rx_ring;

	entl_e1000e_set_rx_mode(adapter->netdev);  // entl version always set Promiscuous mode

	// e1000_restore_vlan(adapter);    No VLAN (rctl.VFe is 0)

	e1000_init_manageability_pt(adapter);   // call it as is

	// We don’t need immediate interrupt on Tx completion. (unless buffer was full and quick responce is required, but it’s not likely)
	e1000_configure_tx(adapter);        // So, call it as is. 

	if (adapter->netdev->features & NETIF_F_RXHASH)
		e1000e_setup_rss_hash(adapter);                   // Can be used as is (multi-queue is disabled)
	
	// my version of setup, with debug output
	entl_e1000_setup_rctl(adapter);

	// my version of configure rx, wth debug output
	entl_e1000_configure_rx(adapter);

	// this function is set in configure rx
	adapter->alloc_rx_buf(rx_ring, e1000_desc_unused(rx_ring), GFP_KERNEL);

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

