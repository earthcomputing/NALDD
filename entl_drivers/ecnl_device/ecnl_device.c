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
#define DRV_VERSION "0.0.1" 

static int nl_ecnl_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info);

static void nl_ecnl_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info);

static struct net_device *ecnl_devices[ECNL_DRIVER_MAX] ;

static int num_ecnl_devices = 0 ;
static int device_busy = 0 ;

static net_device *find_ecnl_device( unsigned char *name ) {
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
	NL_ECNL_MCGRP_DISCOVERY,
	NL_ECNL_MCGRP_TEST
};

static const struct genl_multicast_group nl_ecnd_mcgrps[] = {
	[NL_ECNL_MCGRP_LINKSTATUS] = { .name = NL_ECNL_MULTICAST_GOUP_LINKSTATUS },
	[NL_ECNL_MCGRP_AIT] = { .name = NL_ECNL_MULTICAST_GOUP_AIT },
	[NL_ECNL_MCGRP_DISCOVERY] = { .name = NL_ECNL_MULTICAST_GOUP_DISCOVERY },
	[NL_ECNL_MCGRP_TEST] = { .name = NL_ECNL_MULTICAST_GOUP_TEST },
};

/* policy for the attributes */
static const struct nla_policy nl_ecnl_policy[NL_ECNL_ATTR_MAX+1] = {
	[NL_ECNL_ATTR_DEVICE_NAME] = { .type = NLA_NUL_STRING, .len = 20-1 },
	[NL_ECNL_ATTR_DEVICE_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_PORT_NAME] = { .type = NLA_NUL_STRING, .len = 20-1 },
	[NL_ECNL_ATTR_PORT_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_LINK_STATE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_CURRENT_STATE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_CURRENT_I_KNOW] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_CURRENT_I_SENT] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_FLAG] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_PFLAG] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_STATE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_I_KNOW] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_I_SENT] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_FLAG] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_ERROR_TIME_SEC] = { .type = NLA_U64 },
	[NL_ECNL_ATTR_ERROR_TIME_NSEC] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_NUM_AIT_MESSAGES] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_SIZE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_ID] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_LOCATION] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_CONTENT] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_TABLE_CONTENT_SIZE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_HASH_ENABLE] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_AIT_MESSAGE] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_DISCOVERING_MSG] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_TABLE_ENTRY] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_TABLE_ENTRY_LOCATION] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_PORT_NAMES] = { .type = NLA_NUL_STRING, .len = 256 },
	[NL_ECNL_ATTR_DEVICE_U_ADDR] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_DEVICE_L_ADDR] = { .type = NLA_U32 },
	[NL_ECNL_ATTR_AIT_MESSAGE] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_DISCOVERING_MSG] = { .type = NL_ECNL_ATTR_UNSPEC },
	[NL_ECNL_ATTR_MSG_LENGTH] = { .type = NLA_U32 },

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
static int nl_ecnl_alloc_driver(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct ecnl_device *dev = NULL ;
	struct net_device *n_dev ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
	}
	if( !n_dev ) {
		int index ;
		n_dev = alloc_netdev( sizeof(struct ecnl_device), dev_name, ecnl_setup ) ;
		if( !dev ) rerutn rc ;
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
  				rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
				if( rc == 0 ) {
					rc = genlmsg_unicast(genl_info_net(info), skb, info->snd_pid );
				}
			}		
		}
	}
	return rc ;

}

// Put entt driver link state to the skb 
static int add_link_state( struct sk_buff *skb, struct ecnl_device *dev, char *name, struct entl_ioctl_data *state)
{
	int rc;
  	rc = nla_put_string(skb, NL_ECNL_ATTR_DEVICE_NAME, dev->name);
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
  	if(rc) return rc ;
  	rc = nla_put_string(skb, NL_ECNL_ATTR_PORT_NAME, name);
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_LINK_STATE, state->link_state) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_CURRENT_STATE, state->state.current_state ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_CURRENT_I_KNOW, state->state.event_i_know ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_CURRENT_I_SENT, state->state.event_i_sent ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_STATE, state->error_state.current_state ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_COUNT, state->error_state.error_count ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_FLAG, state->error_state.error_flag ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_PFLAG, state->error_state.p_error_flag ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_STATE, state->error_state.current_state ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_I_KNOW, state->error_state.event_i_know ) ;
  	if(rc) return rc ;
  	rc = nla_put_u64(skb, NL_ECNL_ATTR_ERROR_TIME_SEC, state->error_state.error_time.seconds ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_ERROR_TIME_NSEC, state->error_state.error_time.nanoseconds ) ;
  	if(rc) return rc ;
  	rc = nla_put_u32(skb, NL_ECNL_ATTR_NUM_AIT_MESSAGES, state->num_queued) ;
  	return rc ;
}

// NL_ECNL_CMD_GET_STATE: get the link state 
static int nl_ecnl_get_state(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		char *port_name ;
		int i ;
		na = info->attrs[NL_ECNL_ATTR_PORT_NAME];
		if (na) {
			port_name = (char *)nla_data(na);
			for( i = 0 ; i < dev->num_drivers ; i++ ) {
				struct entl_driver *driver = &dev->drivers[i] ;
				if( strcmp(driver->name, port_name ) == 0 ) {
					struct entl_ioctl_data state ;
					int err = driver->funcs->get_entl_state(driver, &state ) ;
					if( !err ) {
						// return data packet back to caller
						struct sk_buff *rskb;
						rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
						if ( rskb ) {
							void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_STATE);
							rc = add_link_state(rskb, dev, port_name, &state) ;
							if( rc == 0 ) {
								rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
							}
						}
					}
					break ;
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_SIZE];
		if( !na ) return rc ;

		size = (u32)nla_data(na);
		for( id = 0 ; id < ENTL_TABLE_MAX ; id++ ) {
			if( dev->ecnl_tables[id] == NULL ) {
				ecnl_table_entry *ecnl_table = kzalloc( sizeof(struct ecnl_table_entry) * size , GFP_ATOMIC );
				if( ecnl_table ) {
					struct sk_buff *rskb;
					dev->ecnl_tables[id] = ecnl_table ;
					dev->ecnl_tables_size[id] = size ;
					rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
					if ( rskb ) {
						void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_ALLOC_TABLE);
  						rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
  						if( rc ) return rc ;
  						rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
						if( rc == 0 ) {
							rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id, location, size, t_size ;
		ecnl_table_entry *ecnl_table ;
		struct sk_buff *rskb;
		char *cp ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		ecnl_table_entry *ecnl_table = dev->ecnl_tables[id] ;
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
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id, location, t_size ;
		ecnl_table_entry *ecnl_table ;
		struct sk_buff *rskb;
		char *entry ;
		na = info->attrs[NL_ECNL_ATTR_TABLE_ID];
		if( !na ) return rc ;
		id = (u32)nla_data(na);
		ecnl_table_entry *ecnl_table = dev->ecnl_tables[id] ;
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
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		unsigned long flags ;
		int id, h_en ;
		ecnl_table_entry *ecnl_table ;
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
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
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
			kfree(dev->ecnl_tables[id]) ;
			dev->ecnl_tables[id] = NULL ;
		}
		dev->ecnl_tables_size[id] = 0 ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_DEALLOC_TABLE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_map_port_names(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int i ;
		int len = 0 ;
		char *cp, *tp;
		struct sk_buff *rskb;
		for( i = 0 ; i < dev->num_drivers ; i++ ) {
			len += strlen( dev->drivers[i].name ) + 2 ;
		}
		tp = cp = kzalloc( len+1 , GFP_ATOMIC );
		for( i = 0 ; i < dev->num_drivers ; i++ ) {
			sprintf( tp, "%s,", dev->drivers[i].name ) ;
			tp += strlen( dev->drivers[i].name ) + 1 ;
		}
		tp -- ;
		*tp = 0 ;
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_PORT_NAMES);
  			rc = nla_put_string(rskb, NL_ECNL_ATTR_PORT_NAMES, cp);
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_U_ADDR, dev->u_addr) ;
			if( rc ) return rc ;
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_L_ADDR, dev->l_addr) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
			}
		}
		kfree(cp) ;
	}
	return rc ;
}

static struct entl_driver* find_driver( struct ecnl_device* dev, char *name ) {
	int i ;
	for( i = 0 ; i < dev->num_drivers ; i++ ) {
		if( strcmp(dev->drivers[i].name, name) == 0 ) {
			return &dev->drivers[i] ;
		}
	}
	return NULL ;
}


static int nl_ecnl_map_ports(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		int id ;
		struct sk_buff *rskb;
		for( id = 0 ; id < ECNL_DRIVER_MAX ; id++ ) {
			dev->drivers_map[id] = NULL ;
		}
		id = 0 ;
		char *cp, *tp;
		na = info->attrs[NL_ECNL_CMD_GET_PORT_NAMES];
		if( !na ) return rc ;
		tp = cp = (char *)nla_data(na);
		while( *tp ){
			if( *tp == ',' ) {
				struct entl_driver* driver ;
				*tp = 0 ;
				driver = find_driver( dev, cp ) ;
				if( driver ) {
					dev->drivers_map[id++] = driver ;	
				}
				cp = tp + 1 ;
			}
			tp++ ;
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_MAP_PORTS);
			rc = nla_put_u32(skb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
			}
		}
	}
	return rc ;
}

static int nl_ecnl_start_forwarding(struct sk_buff *skb, struct genl_info *info)
{
	int rc = -1 ;
	struct nlattr *na;
	struct net_device *n_dev ;
	struct ecnl_device *dev = NULL ;
	char *dev_name ;
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
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
			msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_START_FORWARDING);
			rc = nla_put_u32(skb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
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
			msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_STOP_FORWARDING);
			rc = nla_put_u32(skb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct entl_driver *driver ;
		struct entt_ioctl_ait_data ait_data ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		if( driver->funcs ) {
			na = info->attrs[NL_ECNL_ATTR_AIT_MESSAGE];
			memcpy( &ait_data, (char*)nla_data(na), sizeof(struct entt_ioctl_ait_data)) ;
			driver->funcs->send_AIT_message( driver->device, ait_data )
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_SEND_AIT_MESSAGE);
			rc = nla_put_u32(skb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put(skb, NL_ECNL_ATTR_AIT_MESSAGE, sizeof(struct entt_ioctl_ait_data), ait_data) ;

			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
		if( na ) {
			int id = (int)nla_data(na);
			n_dev = ecnl_devices[id] ;
			dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
		}
	}
	if( dev ) {
		u32 port_id ;
		struct entl_driver *driver ;
		struct entt_ioctl_ait_data ait_data ;
		struct sk_buff *rskb;
		na = info->attrs[NL_ECNL_ATTR_PORT_ID];
		if( !na ) return rc ;
		port_id = (u32)nla_data(na);
		driver = &dev->drivers[port_id] ;
		if( driver != NULL && driver->funcs ) {
			driver->funcs->retrieve_AIT_message( driver->device, ait_data )
		}
		rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
		if ( rskb ) {
			msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
			rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, dev->index) ;
			if( rc ) return rc ;
			rc = nla_put(skb, NL_ECNL_ATTR_AIT_MESSAGE, sizeof(struct entt_ioctl_ait_data), ait_data) ;
			if( rc == 0 ) {
				rc = genlmsg_unicast(genl_info_net(info), rskb, info->snd_pid );
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
	na = info->attrs[NL_ECNL_ATTR_DEVICE_NAME];
	if (na) {
		dev_name = (char *)nla_data(na);
		n_dev = find_ecnl_device( dev_name ) ;
		dev =  (struct ecnl_device*)netdev_priv(n_dev) ;
	}
	else {
		na = info->attrs[NL_ECNL_ATTR_DEVICE_ID];
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
		na = info->attrs[NL_ECNL_ATTR_MSG_LENGTH];
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
		.cmd = NL_ECNL_CMD_GET_STATE,
		.doit = nl_ecnl_get_state,
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
		.cmd = NL_ECNL_CMD_GET_PORT_NAMES,
		.doit = nl_ecnl_get_port_names,
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

static int encl_register_driver( int encl_id, unsigned char *name, struct net_device *device, u16 u_addr, u32 l_addr, struct entl_driver_funcs *funcs ) ;
{
	unsigned long flags ;
	int index = -1 ;
	struct ecnl_device *dev ;
	if( ecnl_devices[encl_id] == NULL ) {
		ECNL_DEBUG( "register_entl_driver device %d not found\n", encl_id ) ;
		return -1 ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	spin_lock_irqsave( &dev->drivers_lock, flags ) ;

	ECNL_DEBUG( "register_entl_driver %s on %d\n", name, dev->num_drivers ) ;
	if( dev->num_drivers < ECNL_DRIVER_MAX ) {
		index = dev->num_driver++ ;
		dev->drivers[index].name = name ;
		dev->drivers[index].device = device ;
		dev->drivers[index].funcs = funcs ;
		// pick the largest address as this device address
		if( dev->u_addr < u_addr || (dev->u_addr == u_addr && dev->l_addr < l_addr) ) {
			dev->u_addr = u_addr ;
			dev->l_addr = l_addr ;
		}		
	}
	else {
		ECNL_DEBUG( "register_entl_driver driver table overflow %d\n", dev->num_drivers ) ;
	}
	spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
	return index ;
}

static void encl_deregister_drivers( int encl_id )
{
	unsigned long flags ;
	int i ;
	struct ecnl_device *dev ;
	if( ecnl_devices[encl_id] == NULL ) {
		ECNL_DEBUG( "encl_deregister_driver device %d not found\n", encl_id ) ;
		return ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	spin_lock_irqsave( &dev->drivers_lock, flags ) ;
	dev->num_drivers = 0 ;
	spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
}

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

static int ecnl_receive_skb( int encl_id, int index, struct sk_buff *skb ) {
	unsigned long flags ;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev ;
	ecnl_table_entry_t entry ;
	u32 id ;
	u8 direction ;
	bool alloc_pkt = false ;
	if( ecnl_devices[encl_id] == NULL ) {
		ECNL_DEBUG( "ecnl_receive_skb device %d not found\n", encl_id ) ;
		return -1 ;
	}
	dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	id = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
	direction = eth->h_souce[0] & 0x80 ;
	if( dev->fw_enable && dev->current_table && dev->current_table_size < id ) {
		u16 port_vector ;
		spin_lock_irqsave( &dev->drivers_lock, flags ) ;
		memcpy( &entry, &dev->current_table[id], sizeof(ecnl_table_entry_t)) ;
		spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
		port_vector = entry.info.port_vector ;
		if( direction == 0 ) {
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
				port_vector = (port_vector >> 1) ;
				for( i = 0 ; i < 15 ; i++ ) {
					if( port_vector & 1 ) {
						struct net_device *nexthop = dev->drivers_map[i].device ;
						struct entl_driver_funcs *funcs = dev->drivers_map[i].func ;
						if( nexthop && funcs ) {
							// forwarding to next driver
							if( port_vector & 0xfffe ) {
								struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
								*eth = (struct ethhdr *)skbc->data ;
								funcs->start_xmit(skbc, nexthop) ;
							}
							else {
								funcs->start_xmit(skb, nexthop) ;
							}
						}

					}
					port_vector = (port_vector >> 1) ;
				}
			}			
		}
		else {
			if( port_vector & 1 ) {
				// send to this host
				if( entry.info.parent > 0 ) {
					// more parent to send packet
					struct sk_buff *skbc = skb_clone( skb, GFP_ATOMIC ) ;
					netif_rx(skbc);
				}
	    		else netif_rx(skb);
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
		msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, encl_id) ;
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
static void encl_link_status_update( int encl_id, int index, struct entl_ioctl_data *state ) 
{
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	struct entl_driver *driver = &dev->drivers[index] ;

	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		void *msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_STATE);
		rc = add_link_state(rskb, dev, port_name, state) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_LINKSTATUS, GFP_KERNEL );
		}
	}
	return rc ; 	


}

static void encl_got_ait_message( int encl_id, int index, struct entt_ioctl_ait_data* ait_data ) 
{
	int rc = -1 ;
	unsigned long flags ;
	struct sk_buff *rskb;
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(ecnl_devices[encl_id]) ;
	rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if ( rskb ) {
		msg_head = genlmsg_put(rskb, 0, info->snd_seq + 1, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_DEVICE_ID, encl_id) ;
		if( rc ) return rc ;
		rc = nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index) ;
		if( rc ) return rc ;
		
		rc = nla_put(rskb, NL_ECNL_ATTR_AIT_MESSAGE, sizeof(struct entt_ioctl_ait_data), ait_data) ;
		if( rc == 0 ) {
			rc = genlmsg_multicast_allns( &nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL );
		}
	}
	return rc ; 

}

static struct ecnl_device_funcs ecnl_funcs =
{
	.driver_index = encl_driver_index,
	.register_driver = encl_register_driver,
	.receive_skb = ecnl_receive_skb,
	.receive_dsc = ecnl_receive_dsc,
	.link_status_update = encl_link_status_update,
	.got_ait_message = encl_got_ait_message, 
	.deregister_drivers = encl_deregister_drivers,
} ;

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
	struct ethhdr *eth = (struct ethhdr *)skb->data ;
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(n_dev) ;
	u16 u_addr ;
	u32 l_addr, entry, offset ;
	if( dev == NULL ) {
		ECNL_DEBUG( "ecnl_hard_start_xmit device %d not found\n", encl_id ) ;
		return -1 ;
	}
	u_addr = (u16)eth->h_source[0] << 8 | (u32)eth->h_source[1] ;
	l_addr = (u32)eth->h_source[2] << 24 | (u32)eth->h_source[3] << 16 | (u32)eth->h_source[4] << 8 | (u32)eth->h_source[5] ;
	if( dev->fw_enable && dev->current_table ) {
		spin_lock_irqsave( &dev->drivers_lock, flags ) ;
		entry =  ecnl_table_lookup( dev, u_addr, l_addr ) ;
		spin_unlock_irqrestore( &dev->drivers_lock, flags ) ;
		if( entry == 0xff ) {
			ECNL_DEBUG( "ecnl_hard_start_xmit table lookup failed %04x %08x\n", u_addr, l_addr ) ;
			return -1 ;
		}
		else {
			if( entry == 0 ) {
				// send to this host
				ECNL_DEBUG( "ecnl_hard_start_xmit %s forwarding packet to self\n", dev->name ) ;
    			netif_rx(skb);
			}
			else {
				struct net_device *nexthop = drivers_map.drivers_map[entry-1].device ;
				struct entl_driver_funcs *funcs = drivers_map.drivers_map[entry-1].func ;
				if( nexthop && funcs ) {
					// forwarding to next driver
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

static struct net_device_stats *ecnl_get_stats( struct net_device *dev)
{
	struct ecnl_device *dev = (struct ecnl_device*)netdev_priv(n_dev) ;

	return return &dev->net_stats ;
}


static void ecnl_setup( struct net_device *dev ) {
	dev->open = ecnl_open ;
	dev->stop = ecnl_stop ;
	dev->hard_start_xmit = ecnl_hard_start_xmit ;
	dev->hard_header = ecnl_hard_header ;
	dev->rebuild_header = ecnl_rebuild_header ;
	dev->tx_timeout = ecnl_tx_timeout ;
	dev->get_stats = scnl_get_stats ;

	dev->flags |= IFF_NOARP ;
}

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
			e1000e_driver_version);

		pr_info("(Develop 5)\n" ) ;
		pr_info("Copyright(c) 2016 Earth Computing.\n");

        err = genl_register_family_with_ops_groups(&nl_ecnd_fam, nl_ecnl_ops, nl_ecnd_mcgrps);
        if( err ) {
  			ECNL_DEBUG( "ecnl_init_module failed to register genetlink family %s\n", nl_ecnd_fam.name ) ;
  			goto error_exit ;
        }
        else {
  			ECNL_DEBUG( "ecnl_init_module registered genetlink family %s\n", nl_ecnd_fam.name ) ;        	
        }

		ecnl_devices[num_ecnl_devices++] = n_dev = alloc_netdev( sizeof(struct ecnl_device), MAIN_DRIVER_NAME, ecnl_setup )

		this_device = (struct ecnl_device*)netdev_priv(n_dev) ;
		memset(this_device, 0, sizeof(struct ecnl_device));
		strcpy( this_device->name, MAIN_DRIVER_NAME) ;
		this_device->index = 0 ;

  		spin_lock_init( &this_device->drivers_lock ) ;
  		device_busy = 1 ;

  		inter_module_register("ecnl_driver_funcs", THIS_MODULE, ecnl_funcs);
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
		inter_module_unregister("ecnl_driver_funcs");

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
