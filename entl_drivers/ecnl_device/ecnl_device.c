/* 
 * Earth Computing Network Link Device 
 * Copyright(c) 2016, 2018 Earth Computing.
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

#include "ecnl_device.h"

#define DRV_NAME	"ecnl"
#define DRV_VERSION "0.0.1" 

static int nl_ecnl_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info);

static void nl_ecnl_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info);

static void ecnl_setup( struct net_device *dev ) ;

static struct net_device *ecnl_devices[ECNL_DRIVER_MAX] ;

static int num_ecnl_devices = 0 ;
static int device_busy = 0 ;

static struct net_device *find_ecnl_device( unsigned char *name ) {
	int i ;
	for( i = 0 ; i < num_ecnl_devices ; i++ ) {
		if( strcmp( name, ecnl_devices[i]->name) == 0 ) {
			return &ecnl_devices[i] ;
		}
	}
	return NULL ;
}

/* the netlink family */
static struct genl_family nl_ecnd_fam = {
	.id = GENL_ID_GENERATE,		/* don't bother with a hardcoded ID */
	.name = ECNL_GENL_NAME,	/* have users key off the name instead */
	.hdrsize = 0,			/* no private header */
	.version = 1,			/* no particular meaning now */
	.maxattr = NL_ECNL_ATTR_MAX,
	.netnsok = true,
	.pre_doit = nl_ecnl_pre_doit,
	.post_doit = nl_ecnl_post_doit,
};

/* multicast groups */
enum nl_ecnd_multicast_groups {
	NL_ECNL_MCGRP_LINKSTATUS,
	NL_ECNL_MCGRP_AIT,
	NL_ECNL_MCGRP_ALO,
	NL_ECNL_MCGRP_DISCOVERY,
	NL_ECNL_MCGRP_TEST
};

static const struct genl_multicast_group nl_ecnd_mcgrps[] = {
	[NL_ECNL_MCGRP_LINKSTATUS] = { .name = NL_ECNL_MULTICAST_GOUP_LINKSTATUS },
	[NL_ECNL_MCGRP_AIT] = { .name = NL_ECNL_MULTICAST_GOUP_AIT },
	[NL_ECNL_MCGRP_ALO] = { .name = NL_ECNL_MULTICAST_GOUP_ALO },
	[NL_ECNL_MCGRP_DISCOVERY] = { .name = NL_ECNL_MULTICAST_GOUP_DISCOVERY },
	[NL_ECNL_MCGRP_TEST] = { .name = NL_ECNL_MULTICAST_GOUP_TEST },
};

/* policy for the attributes */
static const struct nla_policy nl_ecnl_policy[NL_ECNL_ATTR_MAX+1] = {
	[NL_ECNL_ATTR_MODULE_NAME] = { .type = NLA_NUL_STRING, .len = 20-1 },
	[NL_ECNL_ATTR_MODULE_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_NUM_PORTS] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_PORT_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_PORT_NAME] = { .type = NLA_NUL_STRING, .len = 20-1 },
	[NL_ECNL_ATTR_PORT_LINK_STATE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_PORT_S_COUNTER] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_PORT_R_COUNTER] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_PORT_RECOVER_COUNTER] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_PORT_RECOVERED_COUNTER] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_PORT_ENTT_COUNT] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_PORT_AOP_COUNT] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_TABLE_SIZE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_LOCATION] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_CONTENT] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_TABLE_CONTENT_SIZE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_ENTRY] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_TABLE_ENTRY_LOCATION] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_MAP] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_MESSAGE] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_DISCOVERING_MSG] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_MESSAGE_LENGTH] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ALO_REG_VALUES] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_ALO_FLAG] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ALO_REG_DATA] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_ALO_REG_NO] = { .type = NLA_U32 }

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

// NL_ECNL_CMD_ALLOC_DRIVER: Allocate new ENCL driver and returns the index
//  This feature can be used for simulation of the EC Link network in a single kernel image
static int nl_ecnl_alloc_driver(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct ecnl_device *dev = NULL ;
	struct net_device *n_dev ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
	}
	if( !n_dev ) {
		int index ;
		n_dev = alloc_netdev( sizeof(struct ecnl_device), dev_name, NET_NAME_UNKNOWN, ecnl_setup ) ;
		if( !dev ) return rc ;
		index = num_ecnl_devices++ ;
		ecnl_devices[index] = n_dev ;
		dev = (struct ecnl_device*)netdev_priv(n_dev) ;
		memset(dev, 0, sizeof(struct ecnl_device));
		strcpy( dev->name, dev_name) ;
		dev->index = index ;
  		spin_lock_init( &dev->drivers_lock ) ;

		{
			struct sk_buff *rskb;
			rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
			if ( rskb ) {
				void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_ALLOC_DRIVER);
  				rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
				if( rc == 0 ) {
					rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
				}
			}		
		}
	}
	return rc ;

}

// 	NL_ECNL_CMD_GET_MODULE_INFO:  get module info. name, index, num_ports
static int nl_ecnl_get_module_info(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct ecnl_device *dev = NULL ;
	struct net_device *n_dev ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		struct sk_buff *rskb;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_MODULE_INFO);
  			rc = nla_put_string(rskb, NL_ECNL_ATTR_MODULE_NAME, dev->name);
  			if(rc) return rc ;
  			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
  			if(rc) return rc ;
  			rc = nla_put_u32(rskb, NL_ECNL_ATTR_NUM_PORTS, dev->num_ports) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}

	}
	return rc ;
}

// Put entt driver link state to the skb 
static int add_link_state( struct sk_buff *skb, struct ecnl_device *dev, struct entl_driver *port, struct ec_state *state)
{
	int rc;
  	rc = nla_put_string(skb, NL_ECNL_ATTR_MODULE_NAME, dev->name);
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_PORT_ID, port->index) ;
  	if(rc) return rc ;
  	rc = nla_put_string(skb, NL_ECNL_ATTR_PORT_NAME, port->name);
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_PORT_LINK_STATE, state->link_state) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_S_COUNTER, state->s_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_R_COUNTER, state->r_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_RECOVER_COUNTER, state->recover_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_RECOVERED_COUNTER, state->recovered_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_ENTT_COUNT, state->recover_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_PORT_AOP_COUNT, state->recovered_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_NUM_AIT_MESSAGES, state->num_queued) ;
  	return rc ;
}

// NL_ECNL_CMD_GET_PORT_STATE: get the link state 
static int nl_ecnl_get_port_state(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int port_id ;
		int i ;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if (na) {
			port_id = (int)nla_data(na);
			if( port_id < dev->num_ports ) {
				struct ec_state state ;
				struct entl_driver *driver = &dev->drivers[port_id] ;
				int err = driver->funcs->get_entl_state( driver->device, &state ) ;
				if( !err ) {
					// return data packet back to caller
					struct sk_buff *rskb;
					rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
					if ( rskb ) {
						void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_PORT_STATE);
						rc = add_link_state(rskb, dev, driver, &state) ;
						if( rc == 0 ) {
							rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
						}
					}
				}
			}
		}
	}
	return rc ;
}

static int nl_ecnl_alloc_table(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id ;
		u32 size ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_SIZE];
		if( !na ) return rc ;

		size = (u32)nla_data(na);
		for( id = 0 ; id < ENTL_TABLE_MAX ; id++ ) {
			if( dev->ecnl_tables[id] == NULL ) {
				ecnl_table_entry_t *ecnl_table = kzalloc( sizeof(struct ecnl_table_entry) * size , GFP_ATOMIC );
				if( ecnl_table ) {
					struct sk_buff *rskb;
					dev->ecnl_tables[id] = ecnl_table ;
					dev->ecnl_tables_size[id] = size ;
					rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
					if ( rskb ) {
						void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_ALLOC_TABLE);
  						rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
  						if( rc ) return rc ;
  						rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
						if( rc == 0 ) {
							rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
						}
					}
				}
				break ;
			}
		}
	}	
	return rc ;
}

static int nl_ecnl_fill_table(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id, location, size, t_size ;
		ecnl_table_entry_t *ecnl_table ;
		struct sk_buff *rskb;
		char *cp ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		ecnl_table = dev->ecnl_tables[id] ;
		t_size = dev->ecnl_tables_size[id] ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_LOCATION];
		if( !na ) return rc ;
		location = (u32)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_TABLE_CONTENT_SIZE];
		if( !na ) return rc ;
		size = (u32)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_TABLE_CONTENT];
		if( !na ) return rc ;
		if( location + size > t_size ) return rc ;
		cp = (char *)&ecnl_table[location] ;
		memcpy( cp, (char*)nla_data(na), sizeof(struct ecnl_table_entry) * size ) ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_FILL_TABLE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_fill_table_entry(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id, location, t_size ;
		ecnl_table_entry_t *ecnl_table ;
		struct sk_buff *rskb;
		char *entry ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		ecnl_table = dev->ecnl_tables[id] ;
		t_size = dev->ecnl_tables_size[id] ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ENTRY_LOCATION];
		if( !na ) return rc ;
		location = (u32)nla_data(na);
		if( location > t_size ) return rc ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ENTRY];
		if( !na ) return rc ;
		entry = (char*)nla_data(na) ;
		memcpy( (char*)&ecnl_table[location], entry, sizeof(struct ecnl_table_entry) );
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_FILL_TABLE_ENTRY);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_select_table(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		unsigned long flags ;
		int id, h_en ;
		ecnl_table_entry_t *ecnl_table ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		ecnl_table = dev->ecnl_tables[id] ;
		if( ecnl_table ) {
			spin_lock_irqsave( &dev->drivers_lock, flags ) ;
			dev->current_table = ecnl_table ;
			spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_SELECT_TABLE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_dealloc_table(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id ;
		struct sk_buff *rskb;
		char *cp ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		if( dev->ecnl_tables[id] ) {
			if( dev->current_table == dev->ecnl_tables[id]) {
				dev->fw_enable = 0 ;
				dev->current_table = NULL ;
			}
			kfree(dev->ecnl_tables[id]) ;
			dev->ecnl_tables[id] = NULL ;
		}
		dev->ecnl_tables_size[id] = 0 ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_DEALLOC_TABLE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

// NL_ECNL_CMD_MAP_PORTS
static int nl_ecnl_map_ports(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int i ;
		int len = 0 ;
		u32 *mp;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_TABLE_MAP];
		if( na ) {
			mp = (int*)nla_data(na);
			for( i = 0 ; i < ENCL_FW_TABLE_ENTRY_ARRAY ; i++ ) {
				dev->fw_map[i] = mp[i] ;
			}
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_MAP_PORTS);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static struct entl_driver* find_driver( struct ecnl_device* dev, char *name ) {
	int i ;
	for( i = 0 ; i < dev->num_ports ; i++ ) {
		if( strcmp(dev->drivers[i].name, name) == 0 ) {
			return &dev->drivers[i] ;
		}
	}
	return NULL ;
}

static int nl_ecnl_start_forwarding(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		struct sk_buff *rskb;
		dev->fw_enable = true ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_START_FORWARDING);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_stop_forwarding(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		struct sk_buff *rskb;
		dev->fw_enable = false ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_STOP_FORWARDING);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_send_ait_message(struct sk_buff *skb, struct genl_info *info)
{

	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct entl_driver *driver ;
		struct ec_ait_data ait_data ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		na = info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH];
		if( !na ) return rc ;
		ait_data.message_len = (u32)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_MESSAGE];
		if( !na ) return rc ;
		memcpy( ait_data.data, (char*)nla_data(na), ait_data.message_len ) ;
		if( driver->funcs ) {
			driver->funcs->send_AIT_message( driver->device, &ait_data ) ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_SEND_AIT_MESSAGE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id) ;
			//rc = nla_put(skb, NL_ECNL_ATTR_AIT_MESSAGE, sizeof(struct entt_ioctl_ait_data), ait_data) ;

			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_retrieve_ait_message(struct sk_buff *skb, struct genl_info *info)
{

	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct ec_alo_reg alo_reg ;
		struct entl_driver *driver ;
		struct ec_ait_data ait_data ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_DATA] ;
		alo_reg.reg = (uint64_t)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_NO] ;
		alo_reg.index = (u32)nla_data(na);
		if( driver != NULL && driver->funcs ) {
			driver->funcs->retrieve_AIT_message( driver->device, &ait_data ) ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MESSAGE_LENGTH, ait_data.message_len) ;
			if( rc ) return rc ;
			rc = nla_put(rskb, NL_ECNL_ATTR_MESSAGE, ait_data.message_len, ait_data.data ) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}
	return rc ;
}


static int nl_ecnl_write_alo_register(struct sk_buff *skb, struct genl_info *info)
{

	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct ec_alo_reg alo_reg ;
		struct entl_driver *driver ;
		struct ec_ait_data ait_data ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_DATA] ;
		alo_reg.reg = (uint64_t)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_NO] ;
		alo_reg.index = (u32)nla_data(na);
		if( driver != NULL && driver->funcs ) {
			driver->funcs->write_alo_reg( driver->device, &alo_reg ) ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_WRITE_ALO_REGISTER);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}

	return rc ;
}

static int nl_ecnl_read_alo_registers(struct sk_buff *skb, struct genl_info *info)
{

	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct ec_alo_reg alo_reg ;
		struct entl_driver *driver ;
		struct ec_alo_regs alo_regs ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_DATA] ;
		alo_reg.reg = (uint64_t)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_ALO_REG_NO] ;
		alo_reg.index = (u32)nla_data(na);
		if( driver != NULL && driver->funcs ) {
			driver->funcs->read_alo_regs( driver->device, &alo_regs ) ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_READ_ALO_REGISTERS);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_ALO_FLAG, alo_regs.flags) ;
			if( rc ) return rc ;
			rc = nla_put(rskb, NL_ECNL_ATTR_ALO_REG_VALUES, sizeof(uint64_t)*32, alo_regs.regs ) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_portid );
			}
		}
	}

	return rc ;
}

static int nl_ecnl_send_discover_message(struct sk_buff *skb, struct genl_info *info )
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_MODULE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_MODULE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct entl_driver *driver ;
		char *data ;
		u32 len ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		if( driver == NULL || driver->funcs == NULL ) {
			return rc ;
		}
		na = info->attrs[NL_ECNL_ATTR_DISCOVERING_MSG];
		if( !na ) return rc ;
		data = (char *)nla_data(na);
		na = info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH];
		if( !na ) return rc ;
		len = (u32)nla_data(na) ;
		rskb = alloc_skb(len,  GFP_ATOMIC) ;
		if( rskb == NULL ) return rc ;
		rskb->len = len ;
		memcpy( rskb->data, data, len ) ;
		// sending to the device 
		driver->funcs->start_xmit(rskb, driver) ;
	}

	return rc ;
}


static const struct genl_ops nl_ecnl_ops[] = {
	{
		.cmd = NL_ECNL_CMD_ALLOC_DRIVER,
		.doit = nl_ecnl_alloc_driver,
		.policy = nl_ecnl_policy,
	},
	{
		.cmd = NL_ECNL_CMD_GET_MODULE_INFO,
		.doit = nl_ecnl_get_module_info,
		.policy = nl_ecnl_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL_ECNL_CMD_GET_PORT_STATE,
		.doit = nl_ecnl_get_port_state,
		.policy = nl_ecnl_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL_ECNL_CMD_ALLOC_TABLE,
		.doit = nl_ecnl_alloc_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_FILL_TABLE,
		.doit = nl_ecnl_fill_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_FILL_TABLE_ENTRY,
		.doit = nl_ecnl_fill_table_entry,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_SELECT_TABLE,
		.doit = nl_ecnl_select_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_DEALLOC_TABLE,
		.doit = nl_ecnl_dealloc_table,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_MAP_PORTS,
		.doit = nl_ecnl_map_ports,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_START_FORWARDING,
		.doit = nl_ecnl_start_forwarding,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_STOP_FORWARDING,
		.doit = nl_ecnl_stop_forwarding,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_SEND_AIT_MESSAGE,
		.doit = nl_ecnl_send_ait_message,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE,
		.doit = nl_ecnl_retrieve_ait_message,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_WRITE_ALO_REGISTER,
		.doit = nl_ecnl_write_alo_register,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_ECNL_CMD_READ_ALO_REGISTERS,
		.doit = nl_ecnl_read_alo_registers,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},

	{
		.cmd = NL_ECNL_CMD_SEND_DISCOVER_MESSAGE,
		.doit = nl_ecnl_send_discover_message,
		.policy = nl_ecnl_policy,
		.flags = GENL_ADMIN_PERM,
	},

};

static int encl_driver_index( unsigned char *encl_name ) {
	unsigned long flags ;
	int index = -1 ;
	struct ecnl_device *dev = NULL ;
	struct net_device *n_dev = find_ecnl_device( encl_name ) ;
	if( dev == NULL ) {
		ECNL_DEBUG( "register_entl_driver device %s not found\n", encl_name ) ;
		return -1 ;
	}
	dev = (struct ecnl_device*)netdev_priv( n_dev ) ;
	return dev->index ;
}

static int encl_register_port( int ecnl_id, unsigned char *name, struct net_device *device, struct entl_driver_funcs *funcs ) 
{
	unsigned long flags ;
	int index = -1 ;
	struct ecnl_device *dev ;
	if( ecnl_devices[ecnl_id] == NULL ) {
		ECNL_DEBUG( "register_entl_driver device %d not found\n", ecnl_id ) ;
		return -1 ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[ecnl_id]) ;
	spin_lock_irqsave( &dev->drivers_lock, flags ) ;

	ECNL_DEBUG( "register_entl_driver %s on %d\n", name, dev->num_ports ) ;
	if( dev->num_ports < ECNL_DRIVER_MAX ) {
		index = dev->num_ports++ ;
		dev->drivers[index].name = name ;
		dev->drivers[index].device = device ;
		dev->drivers[index].funcs = funcs ;
		dev->fw_map[index] = index ; // default map by register order
		ECNL_DEBUG( "register_entl_driver module %d driver allocate on index %d\n", ecnl_id, index ) ;
	}
	else {
		ECNL_DEBUG( "register_entl_driver module %d driver table overflow %d\n", ecnl_id, dev->num_ports ) ;
	}
	spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
	return index ;
}

static void encl_deregister_ports( int ecnl_id )
{
	unsigned long flags ;
	int i ;
	struct ecnl_device *dev ;
	if( ecnl_devices[ecnl_id] == NULL ) {
		ECNL_DEBUG( "encl_deregister_driver device %d not found\n", ecnl_id ) ;
		return ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[ecnl_id]) ;
	spin_lock_irqsave( &dev->drivers_lock, flags ) ;
	dev->num_ports = 0 ;
	spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
}

/*
static u8 ecnl_table_lookup( struct ecnl_device *dev, u16 u_addr, u32 l_addr ) 
{
	if( dev->hash_enable ) {
		u16 hash_entry = addr_hash_10(u_addr, l_addr) ;
		while( 1 ) {
			struct ecnl_hash_entry *h_e = &dev->current_hash_table[hash_entry] ;
			if( h_e->u_addr == u_addr && h_e->l_addr == l_addr ) {
				u32 offset = h_e->location & 0xf ;
				ecnl_table_entry e =  dev->current_table[h_e->location >> 4] ;
				return (e >> offset) & 0xf ;
			}
			if( h_e->next == 0 ) return 0xff ; // not found
			hash_entry = h_e->next ;
		}
	}
	else {
		u32 offset = l_addr & 0xf ;
		ecnl_table_entry e = dev->current_table[l_addr>>4] ;
		return (e >> offset) & 0xf ;
	}
}
*/


/*
 *	This is an Ethernet frame header.
 */

//struct ethhdr {
//	unsigned char	h_dest[ETH_ALEN];	/* destination eth addr	*/
//	unsigned char	h_source[ETH_ALEN];	/* source ether addr	*/
//	__be16		h_proto;		/* packet type ID field	*/
//} __attribute__((packed));

static void set_next_id( struct sk_buff *skb, u32 nextID ) 
{
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	eth->h_source[2] = nextID >> 24 ;
	eth->h_source[3] = nextID >> 16 ; 
	eth->h_source[4] = nextID >> 8 ;
	eth->h_source[5] = nextID ;

}

static int ecnl_receive_skb( int encl_id, int index, struct sk_buff *skb ) {
	unsigned long flags ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev ;
	ecnl_table_entry_t entry ;
	u32 id ;
	u8 direction ;
	u8 host_on_backward ;
	u8 parent ;
	u32 nextID ;

	bool alloc_pkt = false ;
	if( ecnl_devices[encl_id] == NULL ) {
		ECNL_DEBUG( "ecnl_receive_skb device %d not found\n", encl_id ) ;
		return -1 ;
	}
	if( eth->h_dest[0] & 0x80 == 0 ) {  // test fw bit
		// no forwarding, send to host
		netif_rx(skb);
		return 0 ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	id = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
	direction = eth->h_source[0] & 0x80 ;
	host_on_backward = eth->h_source[0] & 0x40 ;
	if( dev->fw_enable && dev->current_table && dev->current_table_size < id ) {
		u16 port_vector ;
		spin_lock_irqsave( &dev->drivers_lock, flags ) ;
		memcpy( &entry, &dev->current_table[id], sizeof(ecnl_table_entry_t)) ;
		spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
		port_vector = entry.info.port_vector ;
		if( direction == 0 ) {  // forward direction
			if( port_vector == 0 ) {
				ECNL_DEBUG( "ecnl_receive_skb no forward bit on %d %08x\n", encl_id, index  ) ;
				return -1 ;
			}
			else {
				int i ;

				if( port_vector & 1 ) {
					// send to this host
					if( port_vector & 0xfffe ) {
						// more port to send packet
						struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
						netif_rx(skbc);
					}
	    			else netif_rx(skb);
				}
				// nulti-port forwarding
				port_vector &= ~(u16)(1 << index) ; // avoid to send own port
				port_vector = (port_vector >> 1) ;  // reduce host bit

				for( i = 0 ; i < 15 ; i++ ) {
					if( port_vector & 1 ) {  
						int id = dev->fw_map[i] ;
						struct net_device *nexthop = dev->drivers[id].device ;
						struct entl_driver_funcs *funcs = dev->drivers[id].funcs ;
					    nextID = entry.nextID[id] ;
						if( nexthop && funcs ) {
							// forwarding to next driver
							if( port_vector & 0xfffe ) {
								struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
								set_next_id( skbc, nextID ) ;
								funcs->start_xmit(skbc, nexthop) ;
							}
							else {
								set_next_id( skb, nextID ) ;
								funcs->start_xmit(skb, nexthop) ;
							}
						}

					}
					port_vector = (port_vector >> 1) ;
				}
			}			
		}
		else {  // backward
			parent = entry.info.parent ;
			if( parent == 0 || host_on_backward ) {
				// send to this host
				if( parent > 0 ) {
					// more parent to send packet
					struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
					netif_rx(skbc);
				}
	    		else netif_rx(skb);
			}
			if( parent > 0 ) {
				int id = dev->fw_map[parent] ;
				struct net_device *nexthop = dev->drivers[id].device ;
				struct entl_driver_funcs *funcs = dev->drivers[id].funcs ;
				nextID = entry.nextID[id] ;
				set_next_id( skb, nextID ) ;
				funcs->start_xmit(skb, nexthop) ;

			}
		}
	}
	else {
		ECNL_DEBUG( "ecnl_receive_skb %d can't forward packet with index %d\n", encl_id, id ) ;
		// send to host??
    	netif_rx(skb);
	}
	return 0 ; 
}

// entl driver received a discovery massage
static int ecnl_receive_dsc( int encl_id, int index, struct sk_buff *skb ) {
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		void *msg_head = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, encl_id) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index) ;
		if( rc ) return rc ;
		rc = nla_put(rskb, NL_ECNL_ATTR_DISCOVERING_MSG, skb->len, skb->data) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_DISCOVERY, GFP_KERNEL );
		}
	}
	return rc ; 
}

// entl driver has a link update
static void encl_link_status_update( int encl_id, int index, struct ec_state *state ) 
{
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	struct entl_driver *driver = &dev->drivers[index] ;

	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		void *msg_head = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_PORT_STATE);
		rc = add_link_state(rskb, dev, driver, state) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_LINKSTATUS, GFP_KERNEL );
		}
	}
	return rc ; 	


}

static void encl_got_ait_message( int encl_id, int index, struct ec_ait_data* ait_data ) 
{
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	//struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		void *msg_head = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, encl_id) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_MESSAGE_LENGTH, ait_data->message_len ) ;
		if( rc ) return rc ;
		
		rc = nla_put(rskb, NL_ECNL_ATTR_MESSAGE, sizeof(struct ec_ait_data), ait_data->data) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL );
		}
	}
	return rc ; 

}

static void encl_got_alo_update( int encl_id, int index ) 
{
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	struct ec_alo_regs alo_regs ;
	//struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	struct entl_driver *port = &dev->drivers[index] ;
	port->funcs->read_alo_regs( port->device, &alo_regs ) ;
	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		void *msg_head = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_READ_ALO_REGISTERS);
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, encl_id) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index) ;
		if( rc ) return rc ;
		rc = nla_put(rskb, NL_ECNL_ATTR_ALO_REG_VALUES, sizeof(uint64_t)*32, alo_regs.regs) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_ALO_FLAG, alo_regs.flags) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL );
		}
	}
	return rc ;  

}

static struct ecnl_device_funcs ecnl_api_funcs =
{
	.register_port = encl_register_port,
	.receive_skb = ecnl_receive_skb,
	//.receive_dsc = ecnl_receive_dsc,
	.link_status_update = encl_link_status_update,
	.got_ait_message = encl_got_ait_message, 
	.got_alo_update = encl_got_alo_update, 
	.deregister_ports = encl_deregister_ports
} ;

EXPORT_SYMBOL(ecnl_api_funcs);

// net_device interface functions
static int ecnl_open( struct net_device *dev ) {
	ECNL_DEBUG( "ecnl_open %s\n", dev->name ) ;
	netif_start_queue( dev ) ;
	return 0 ;
}

static int ecnl_stop( struct net_device *dev ) {
	ECNL_DEBUG( "ecnl_stop %s\n", dev->name ) ;
	netif_stop_queue( dev ) ;
	return 0 ;
}


static int ecnl_hard_start_xmit( struct sk_buff *skb, struct net_device *n_dev ) {
	unsigned long flags ;
	ecnl_table_entry_t entry ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(n_dev) ;
	u8 direction ;
	u32 id ;
	u32 nextID ;

	id = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
	direction = eth->h_source[0] & 0x80 ;
	if( dev->fw_enable && dev->current_table && dev->current_table_size < id ) {
		u16 port_vector ;
		spin_lock_irqsave( &dev->drivers_lock, flags ) ;
		memcpy( &entry, &dev->current_table[id], sizeof(ecnl_table_entry_t)) ;
		spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
		port_vector = entry.info.port_vector ;
		if( direction == 0 ) {  // forward direction
			int i ;
			port_vector >>= 1 ; // remove host bit
			for( i = 0 ; i < 15 ; i++ ) {
				if( port_vector & 1 ) {  
					int id = dev->fw_map[i] ;
					struct net_device *nexthop = dev->drivers[id].device ;
					struct entl_driver_funcs *funcs = dev->drivers[id].funcs ;
				    nextID = entry.nextID[id] ;
					if( nexthop && funcs ) {
						// forwarding to next driver
						if( port_vector & 0xfffe ) {
							struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
							set_next_id( skbc, nextID ) ;
							funcs->start_xmit(skbc, nexthop) ;
						}
						else {
							set_next_id( skb, nextID ) ;
							funcs->start_xmit(skb, nexthop) ;
						}
					}

				}
				port_vector = (port_vector >> 1) ;
			}		
		}
		else {  // to parent side
			u8 parent = entry.info.parent ;
			if( parent == 0 ) {
				// send to this host
				ECNL_DEBUG( "ecnl_hard_start_xmit %s forwarding packet to self\n", dev->name ) ;
    			return -1 ;
			}
			else {
				int id = dev->fw_map[parent] ;
				struct net_device *nexthop = dev->drivers[id].device ;
				struct entl_driver_funcs *funcs = dev->drivers[id].funcs ;
				if( nexthop && funcs ) {
					// forwarding to next driver
					nextID = entry.nextID[id] ;
					set_next_id( skb, nextID ) ;
					funcs->start_xmit(skb, nexthop) ;
				}
			}
		}
	}
	else {
		ECNL_DEBUG( "ecnl_hard_start_xmit %s can't forward packet\n", dev->name ) ;
    	return -1 ;
	}
	return 0 ;
}

static int ecnl_hard_header( 
	struct sk_buff *skb, struct net_device *dev, 
	unsigned short type, void *daddr, void *saddr, unsigned len
) {

	return 0 ;
}

static int ecnl_rebuild_header( struct sk_buff *skb ) {

	return 0 ;
}

static void ecnl_tx_timeout( struct net_device *dev ) {

	return 0 ;
}

static struct net_device_stats *ecnl_get_stats( struct net_device *n_dev)
{
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(n_dev) ;

	return &dev->net_stats ;
}

static struct net_device_ops ecnl_netdev_ops =
{
	.ndo_open = ecnl_open,
	.ndo_stop = ecnl_stop,
	.ndo_start_xmit = ecnl_hard_start_xmit,
	.ndo_tx_timeout = ecnl_tx_timeout,
	.ndo_get_stats = ecnl_get_stats
} ;

static void ecnl_setup( struct net_device *dev ) {
	dev->netdev_ops = &ecnl_netdev_ops ;

	dev->flags |= IFF_NOARP ;
}

#define ECNL_DEVICE_DRIVER_VERSION "0.0.0.1"

static int __init ecnl_init_module(void)
{
	int i, err = 0;
	struct net_device *n_dev ;
	struct ecnl_device *this_device ;

	if( device_busy ) {
		ECNL_DEBUG( "ecnl_init_module called on busy state\n" ) ;
		err = -1 ;
	}
	else {
		pr_info("Earth Computing Network Link Driver - %s ",
			ECNL_DEVICE_DRIVER_VERSION);

		pr_info("(Develop 6)\n" ) ;
		pr_info("Copyright(c) 2018 Earth Computing.\n");

        err = genl_register_family_with_ops_groups(&nl_ecnd_fam, nl_ecnl_ops, nl_ecnd_mcgrps);
        if( err ) {
  			ECNL_DEBUG( "ecnl_init_module failed to register genetlink family %s\n", nl_ecnd_fam.name ) ;
  			goto error_exit ;
        }
        else {
  			ECNL_DEBUG( "ecnl_init_module registered genetlink family %s\n", nl_ecnd_fam.name ) ;        	
        }

		ecnl_devices[num_ecnl_devices++] = n_dev = alloc_netdev( sizeof(struct ecnl_device), MAIN_DRIVER_NAME, NET_NAME_UNKNOWN, ecnl_setup ) ;

		this_device = (struct ecnl_device*)netdev_priv(n_dev) ;
		memset(this_device, 0, sizeof(struct ecnl_device));
		strcpy( this_device->name, MAIN_DRIVER_NAME) ;
		this_device->index = 0 ;

  		spin_lock_init( &this_device->drivers_lock ) ;
  		device_busy = 1 ;

  		//inter_module_register("ecnl_driver_funcs", THIS_MODULE, ecnl_funcs);

  		if( register_netdev(n_dev) ) {
  			ECNL_DEBUG( "ecnl_init_module failed to register net_dev %s\n", this_device->name ) ;
  		}
	}

error_exit:

	return err;
}

static void __exit ecnl_cleanup_module(void)
{
	int i ;
	if( device_busy ) {
		ECNL_DEBUG( "ecnl_cleanup_module called\n" ) ;
		//inter_module_unregister("ecnl_driver_funcs");

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
