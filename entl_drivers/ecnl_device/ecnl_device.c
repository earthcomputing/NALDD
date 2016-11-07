/* 
 * Earth Computing Network Link Device 
 * Copyright(c) 2016 Earth Computing.
 *
 *  Author: Atsushi Kasuya
 *
 */
#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/etherdevice.h>
#include <net/net_namespace.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/sock.h>
#include <net/inet_connection_sock.h>
#include <linux/kmod.h>

#include "entl_device.h"

#define DRV_NAME	"ecnl"
#define DRV_VERSION	"1.0"

// #define DRV_VERSION "0.0.1" 

char ecnl_driver_name[] = DRV_NAME ;
const char ecnl_driver_version[] = DRV_VERSION ;

static int nl_ecnl_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info);

static void nl_ecnl_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info);

/* the netlink family */
static struct genl_family nl_ecnd_fam = {
	.id = GENL_ID_GENERATE,		/* don't bother with a hardcoded ID */
	.name = ECND_GENL_NAME,	/* have users key off the name instead */
	.hdrsize = 0,			/* no private header */
	.version = 1,			/* no particular meaning now */
	.maxattr = NL_ECND_ATTR_MAX,
	.netnsok = true,
	.pre_doit = nl_ecnl_pre_doit,
	.post_doit = nl_ecnl_post_doit,
};

/* multicast groups */
enum nl_ecnd_multicast_groups {
	NL_ECND_MCGRP_LINKSTATUS,
	NL_ECND_MCGRP_AIT,
	NL_ECND_MCGRP_TEST
};

static const struct genl_multicast_group nl_ecnd_mcgrps[] = {
	[NL_ECND_MCGRP_LINKSTATUS] = { .name = NL_ECND_MULTICAST_GOUP_LINKSTATUS },
	[NL_ECND_MCGRP_AIT] = { .name = NL_ECND_MULTICAST_GOUP_AIT },
	[NL_ECND_MCGRP_TEST] = { .name = NL_ECND_MULTICAST_GOUP_TEST },
};

/* policy for the attributes */
static const struct nla_policy nl_ecnl_policy[NL_ECND_ATTR_MAX+1] = {
	[NL_ECND_ATTR_DEVICE_NAME] = { .type = NLA_NUL_STRING, .len = 20-1 },
	[NL_ECND_ATTR_LINK_STATE] = { .type = NLA_U32 },
	[NL_ECND_ATTR_LINK_NUMBER] = { .type = NLA_U32 },
	[NL_ECND_ATTR_ERROR_FLAG] = { .type = NLA_U32 },
	[NL_ECND_ATTR_TIME_SEC] = { .type = NLA_U64 },
	[NL_ECND_ATTR_TIME_NSEC] = { .type = NLA_U32 },
	[NL_ECND_ATTR_TABLE_SIZE] = { .type = NLA_U32 },
	[NL_ECND_ATTR_TABLE_ID] = { .type = NLA_U32 },
	[NL_ECND_ATTR_TABLE_LOCATION] = { .type = NLA_U32 },
	[NL_ECND_ATTR_TABLE_CONTENT] = { .type = NL_ECND_ATTR_UNSPEC },
	[NL_ECND_ATTR_TABLE_CONTENT_SIZE] = { .type = NLA_U32 },
	[NL_ECND_ATTR_AIT_MESSAGE] = { .type = NL_ECND_ATTR_UNSPEC },
} ;

static int nl_ecnl_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info)
{
	return 0 ;
}

static void nl_ecnl_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info)
{
	return 0 ;
}

static int nl_ecnl_get_state(struct sk_buff *skb, struct genl_info *info)
{
	
	return 0 ;
}

static int nl_ecnl_alloc_table(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_fill_table(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_select_table(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_dealloc_table(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_map_drivers(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_start_forwarding(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_stop_forwarding(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_send_ait_message(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}

static int nl_ecnl_received_ait_message(struct sk_buff *skb, struct genl_info *info)
{

	return 0 ;
}


static const struct genl_ops nl_ecnl_ops[] = {
	{
		.cmd = NL_ECND_CMD_GET_STATE,
		.doit = nl_ecnl_get_state,
		.policy = nl_ecnl_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL_ECND_CMD_ALLOC_TABLE,
		.doit = nl_ecnl_alloc_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_FILL_TABLE,
		.doit = nl_ecnl_fill_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_SELECT_TABLE,
		.doit = nl_ecnl_select_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_DEALLOC_TABLE,
		.doit = nl_ecnl_dealloc_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_MAP_DRIVERS,
		.doit = nl_ecnl_map_drivers,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_START_FORWARDING,
		.doit = nl_ecnl_start_forwarding,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_STOP_FORWARDING,
		.doit = nl_ecnl_stop_forwarding,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_SEND_AIT_MESSAGE,
		.doit = nl_ecnl_send_ait_message,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECND_CMD_RECEIVED_AIT_MESSAGE,
		.doit = nl_ecnl_received_ait_message,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},

};

static struct ecnl_device this_device ;
static int device_busy = 0 ;

int encl_register_driver( unsigned char *name, struct net_device *device, struct entl_driver_funcs *funcs )
{
	unsigned long flags ;
	int index ;
	spin_lock_irqsave( &this_device.drivers_lock, flags ) ;

	ECNL_DEBUG( "register_entl_driver %s on %d\n", name, this_device.num_drivers ) ;
	if( this_device.num_drivers < ECNL_DRIVER_MAX ) {
		index = this_device.num_driver ;
		this_device.drivers[this_device.num_drivers].name = name ;
		this_device.drivers[this_device.num_drivers].device = device ;
		this_device.drivers[this_device.num_drivers++].funcs = funcs ;		
	}
	else {
		ECNL_DEBUG( "register_entl_driver driver table overflow %d\n", this_device.num_drivers ) ;
		return NULL ;
	}
	spin_unlock_irqrestore( &this_device.drivers_lock, flags ) ;
	return index ;
}

void encl_deregister_driver( int index )
{
	unsigned long flags ;
	int i ;
	spin_lock_irqsave( &this_device.drivers_lock, flags ) ;
	if( this_device.num_drivers && index < this_device.num_drivers ) {
		this_device.num_drivers-- ;
		for( i = index ; i < this_device.num_drivers  ; i++ ) {
			this_device.drivers[i] = this_device.drivers[i+1] ;
		}
	}
	spin_unlock_irqrestore( &this_device.drivers_lock, flags ) ;
}

static int ecnl_receive_skb(struct sk_buff *skb) {
	if( this_device.fw_enable && current_table ) {
		struct ethhdr *eth = (struct ethhdr *)skb->data ;

		u32 index = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
		if( index > this_device.current_table_size ) {
			ECNL_DEBUG( "ecnl_receive_skb table index overflow %d > %d\n", index, this_device.current_table_size ) ;
		}
		else {
			ecnl_table_entry e = this_device.current_table[in]
		}
	}
	return 0 ; 
}

static int __init ecnl_init_module(void)
{
	int i, err = 0;

	if( device_busy ) {
		ECNL_DEBUG( "ecnl_init_module called on busy state\n" ) ;
		err = -1 ;
	}
	else {
		pr_info("Earth Computing Network Link Driver - %s ",
			e1000e_driver_version);

		pr_info("(Develop %s %s)\n", __DATE__, __TIME__ ) ;
		pr_info("Copyright(c) 2016 Earth Computing.\n");

		memset(this_device, 0, sizeof(this_device));
  		spin_lock_init( &this_device.drivers_lock ) ;
  		device_busy = 1 ;

  		inter_module_register("ecnl_register_entl_driver", THIS_MODULE, register_entl_driver);
	}


	return err;
}

static void __exit ecnl_cleanup_module(void)
{
	if( device_busy ) {
		ECNL_DEBUG( "ecnl_cleanup_module called\n" ) ;
		inter_module_unregister("ecnl_register_entl_driver");
	  	device_busy = 0 ;
	}
	else {
		ECNL_DEBUG( "ecnl_cleanup_module called on non-busy state\n" ) ;		
	}
}


module_init(ecnl_init_module);
module_exit(ecnl_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
MODULE_VERSION(DRV_VERSION);
