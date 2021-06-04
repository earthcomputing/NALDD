/*---------------------------------------------------------------------------------------------
 *  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

/*
    ccflags-y += -std=gnu99
    ccflags-y += -Wno-declaration-after-statement
    ccflags-y += -Wno-unused-variable
    ccflags-y += -Wno-unused-function
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include <net/cfg80211.h>
#include <net/genetlink.h>
#include <net/inet_connection_sock.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include <linux/kmod.h>

#include <e1000.h>
#include "ecnl_device.h"

#define DRV_NAME        "ecnl"
#define DRV_VERSION     "0.0.2"

#define ECNL_DEVICE_DRIVER_VERSION "0.0.0.2"

static void dump_skbuff(void *user_hdr);

// be paranoid, probably parses with excess ';'
#define NLAPUT_CHECKED(putattr) { int rc = putattr; if (rc) return rc; }
#define NLAPUT_CHECKED_ZZ(putattr) { int rc = putattr; if (rc) return; }

enum nl_ecnd_multicast_groups {
    NL_ECNL_MCGRP_LINKSTATUS,
    NL_ECNL_MCGRP_AIT,
    NL_ECNL_MCGRP_ALO,
    NL_ECNL_MCGRP_DISCOVERY,
    NL_ECNL_MCGRP_TEST
};

static const struct genl_multicast_group nl_ecnd_mcgrps[] = {
    [NL_ECNL_MCGRP_LINKSTATUS] = { .name = NL_ECNL_MULTICAST_GOUP_LINKSTATUS },
    [NL_ECNL_MCGRP_AIT] =        { .name = NL_ECNL_MULTICAST_GOUP_AIT },
    [NL_ECNL_MCGRP_ALO] =        { .name = NL_ECNL_MULTICAST_GOUP_ALO },
    [NL_ECNL_MCGRP_DISCOVERY] =  { .name = NL_ECNL_MULTICAST_GOUP_DISCOVERY },
    [NL_ECNL_MCGRP_TEST] =       { .name = NL_ECNL_MULTICAST_GOUP_TEST },
};

static const struct nla_policy nl_ecnl_policy[NL_ECNL_ATTR_MAX+1] = {
    [NL_ECNL_ATTR_MODULE_NAME] =                { .type = NLA_NUL_STRING, .len = 20-1 },
    [NL_ECNL_ATTR_MODULE_ID] =                  { .type = NLA_U32 },
    [NL_ECNL_ATTR_NUM_PORTS] =                  { .type = NLA_U32 },
    [NL_ECNL_ATTR_PORT_ID] =                    { .type = NLA_U32 },
    [NL_ECNL_ATTR_PORT_NAME] =                  { .type = NLA_NUL_STRING, .len = 20-1 },
    [NL_ECNL_ATTR_PORT_LINK_STATE] =            { .type = NLA_U32 },
    [NL_ECNL_ATTR_PORT_S_COUNTER] =             { .type = NLA_U64 },
    [NL_ECNL_ATTR_PORT_R_COUNTER] =             { .type = NLA_U64 },
    [NL_ECNL_ATTR_PORT_RECOVER_COUNTER] =       { .type = NLA_U64 },
    [NL_ECNL_ATTR_PORT_RECOVERED_COUNTER] =     { .type = NLA_U64 },
    [NL_ECNL_ATTR_PORT_ENTT_COUNT] =            { .type = NLA_U64 },
    [NL_ECNL_ATTR_PORT_AOP_COUNT] =             { .type = NLA_U64 },
    [NL_ECNL_ATTR_NUM_AIT_MESSAGES] =           { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_SIZE] =                 { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_ID] =                   { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_LOCATION] =             { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_CONTENT] =              { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_TABLE_CONTENT_SIZE] =         { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_ENTRY] =                { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_TABLE_ENTRY_LOCATION] =       { .type = NLA_U32 },
    [NL_ECNL_ATTR_TABLE_MAP] =                  { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_MESSAGE] =                    { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_DISCOVERING_MSG] =            { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_MESSAGE_LENGTH] =             { .type = NLA_U32 },
    [NL_ECNL_ATTR_ALO_REG_VALUES] =             { .type = NL_ECNL_ATTR_UNSPEC },
    [NL_ECNL_ATTR_ALO_FLAG] =                   { .type = NLA_U32 },
    [NL_ECNL_ATTR_ALO_REG_DATA] =               { .type = NLA_U64 },
    [NL_ECNL_ATTR_ALO_REG_NO] =                 { .type = NLA_U32 }
};

static void ecnl_setup(struct net_device *plug_in);

static struct net_device *ecnl_devices[ECNL_DRIVER_MAX];

static int num_ecnl_devices = 0;
static int device_busy = 0;

static struct net_device *find_ecnl_device(unsigned char *name) {
    for (int i = 0; i < num_ecnl_devices; i++) {
        struct net_device *plug_in = ecnl_devices[i];
        if (strcmp(name, plug_in->name) == 0) return plug_in;
    }
    return NULL;
}

static int nl_ecnl_pre_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info) {
    return 0;
}

static void nl_ecnl_post_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info) {
    // return 0;
}

static struct genl_family nl_ecnd_fam = {
    .id = GENL_ID_GENERATE,
    .name = ECNL_GENL_NAME,
    .hdrsize = 0,
    .version = 1,
    .maxattr = NL_ECNL_ATTR_MAX,
    .netnsok = true,
    .pre_doit = nl_ecnl_pre_doit,
    .post_doit = nl_ecnl_post_doit,
};

#define GENLMSG_DATA(nh) ((void *) (((char *) nlmsg_data(nh)) + GENL_HDRLEN))
#define NLA_DATA(na) ((void *) (((char *) (na)) + NLA_HDRLEN))

#define PRINTIT printk
static void dump_skbuff(void *user_hdr) {
    struct nlmsghdr *nh = genlmsg_nlhdr(user_hdr, &nl_ecnd_fam);
    struct genlmsghdr *gh = nlmsg_data(nh);
    struct nlattr *head = (struct nlattr *) GENLMSG_DATA(nh);
    void *after = (void *) (((char *) nh) + nh->nlmsg_len);

    PRINTIT("    nh: %08lx\n", (long) nh);
    PRINTIT("    .nlmsg_len: %d\n", nh->nlmsg_len);
    PRINTIT("    .nlmsg_type: %d\n", nh->nlmsg_type);
    PRINTIT("    .nlmsg_flags: %d\n", nh->nlmsg_flags);
    PRINTIT("    .nlmsg_seq: %d\n", nh->nlmsg_seq);
    PRINTIT("    .nlmsg_pid: %d\n", nh->nlmsg_pid);

    PRINTIT("    gh: %08lx\n", (long) gh);
    PRINTIT("    .cmd: %d\n", gh->cmd);
    PRINTIT("    .version: %d\n", gh->version);
    PRINTIT("    .reserved: %d\n", gh->reserved);

    PRINTIT("\n");
    PRINTIT("    after: %08lx\n", (long) after);
    PRINTIT("    payload: %08lx\n", (long) head);

    // trusts that there's enough space for nla hdr and data
    // FIXME: after could compare against NLA_HDRLEN, then against NLMSG_ALIGN(p->nla_len)
    for (struct nlattr *p = head; after > (void *) p; p = (struct nlattr *) (((char *) p) + NLMSG_ALIGN(p->nla_len))) {
        int nbytes = p->nla_len - NLA_HDRLEN; // ((int) NLA_ALIGN(sizeof(struct nlattr)))
        void *d = NLA_DATA(p);
        PRINTIT("    nla: %08lx .nla_type: %d .nla_len: %d .data: %08lx nbytes: %d align: %d\n", (long) p, p->nla_type, p->nla_len, (long) d, nbytes, NLMSG_ALIGN(p->nla_len));
        PRINTIT("      ");
        // flag: nbytes == 0
        if (nbytes == 1) { PRINTIT("%d (%02x)\n", *(char *) d, *(char *) d); continue; }
        if (nbytes == 2) { PRINTIT("%d (%04x)\n", *(short *) d, *(short *) d); continue; }
        if (nbytes == 4) { PRINTIT("%d (%08x)\n", *(int *) d, *(int *) d); continue; }
        if (nbytes == 8) { PRINTIT("%ld (%016lx)\n", *(long *) d, *(long *) d); continue; }

        for (int i = 0; i < nbytes; i++) {
            PRINTIT("%02x", ((char *) d)[i]);
            if (i % 16 == 15) PRINTIT("\n        ");
        }
        PRINTIT("\n");
    }
}

static struct ecnl_device* fetch_ecnl_device(struct genl_info *info) {
    if (info->attrs[NL_ECNL_ATTR_MODULE_NAME]) {
        char *dev_name = (char *) nla_data(info->attrs[NL_ECNL_ATTR_MODULE_NAME]); // nla_get_nulstring
        struct net_device *plug_in = find_ecnl_device(dev_name);
        struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
        return e_dev;
    }

    if (info->attrs[NL_ECNL_ATTR_MODULE_ID]) {
        u32 id = nla_get_u32(info->attrs[NL_ECNL_ATTR_MODULE_ID]);
        struct net_device *plug_in = ecnl_devices[id];
        struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
        return e_dev;
    }

    return NULL;
}

//  can be used for simulation of EC Link network in a single kernel image
static int nl_ecnl_alloc_driver(struct sk_buff *skb, struct genl_info *info) {
    if (!info->attrs[NL_ECNL_ATTR_MODULE_NAME]) return -EINVAL;

    char *dev_name = (char *) nla_data(info->attrs[NL_ECNL_ATTR_MODULE_NAME]); // nla_get_nulstring
    struct net_device *exists = find_ecnl_device(dev_name);
    if (exists) return -EINVAL;

    struct net_device *plug_in = alloc_netdev(sizeof(struct ecnl_device), dev_name, NET_NAME_UNKNOWN, ecnl_setup);
    if (!plug_in) return -ENOMEM;

    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    memset(e_dev, 0, sizeof(struct ecnl_device));

    int module_id = num_ecnl_devices++;
    ecnl_devices[module_id] = plug_in;
    strcpy(e_dev->name, dev_name);
    e_dev->index = module_id; // module_id
    spin_lock_init(&e_dev->drivers_lock);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_ALLOC_DRIVER);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_get_module_info(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    int flags = 0; // NLM_F_MULTI
    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, flags, NL_ECNL_CMD_GET_MODULE_INFO);
    // dump_skbuff(user_hdr);
    NLAPUT_CHECKED(nla_put_string(rskb, NL_ECNL_ATTR_MODULE_NAME, e_dev->name));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_NUM_PORTS, e_dev->num_ports));
    genlmsg_end(rskb, user_hdr);
    // dump_skbuff(user_hdr);
    return genlmsg_reply(rskb, info);
}

static int add_link_state(struct sk_buff *rskb, struct ecnl_device *e_dev, struct entl_driver *e_driver, struct ec_state *state) {
    NLAPUT_CHECKED(nla_put_string(rskb, NL_ECNL_ATTR_MODULE_NAME, e_dev->name));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, e_driver->index)); // port_id
    NLAPUT_CHECKED(nla_put_string(rskb, NL_ECNL_ATTR_PORT_NAME, e_driver->name));

    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_LINK_STATE, state->link_state));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_S_COUNTER, state->s_count));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_R_COUNTER, state->r_count));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_RECOVER_COUNTER, state->recover_count));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_RECOVERED_COUNTER, state->recovered_count));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_ENTT_COUNT, state->recover_count));
    NLAPUT_CHECKED(nla_put_u64(rskb, NL_ECNL_ATTR_PORT_AOP_COUNT, state->recovered_count));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_NUM_AIT_MESSAGES, state->num_queued));
    return 0;
}

static int nl_ecnl_get_port_state(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);
    if (port_id >= e_dev->num_ports) return -EINVAL;

    // printk(KERN_INFO "nl_ecnl_get_port_state: \"%s\" %d\n", e_dev->name, port_id);

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;

    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    // printk(KERN_INFO "entl_driver: \"%s\" e1000e: %p, funcs: %p\n", e_driver->name, e1000e, funcs);
    if (!e1000e || !funcs) return -EINVAL;

    // printk(KERN_INFO "net_device: \"%s\"\n", e1000e->name);

    struct ec_state state; memset(&state, 0, sizeof(struct ec_state));
    int err = funcs->get_entl_state(e1000e, &state);
    if (err) return -EINVAL;

    // printk(KERN_INFO "reply e_dev: \"%s\" (%d)\n", e_dev->name, e_dev->index); // module_id
    // printk(KERN_INFO "reply e_driver: \"%s\" (%d)\n", e_driver->name, e_driver->index); // port_id

    // printk(KERN_INFO "state:\n");

    // return data packet back to caller
    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_PORT_STATE);
    NLAPUT_CHECKED(add_link_state(rskb, e_dev, e_driver, &state));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_alloc_table(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_TABLE_SIZE]) return -EINVAL;

    u32 size = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_SIZE]);
    for (int id = 0; id < ENTL_TABLE_MAX; id++) {
        if (e_dev->ecnl_tables[id] != NULL) continue;

        ecnl_table_entry_t *ecnl_table = kzalloc(sizeof(struct ecnl_table_entry) * size, GFP_ATOMIC);
        if (!ecnl_table) return -ENOMEM;

        e_dev->ecnl_tables[id] = ecnl_table;
        e_dev->ecnl_tables_size[id] = size;

        struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
        if (!rskb) return -ENOMEM;

        void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_ALLOC_TABLE);
        NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
        NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id));
        genlmsg_end(rskb, user_hdr);
        return genlmsg_reply(rskb, info);
    }

    return -EINVAL;
}

static int nl_ecnl_fill_table(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_TABLE_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_TABLE_LOCATION]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_TABLE_CONTENT_SIZE]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_TABLE_CONTENT]) return -EINVAL;

    u32 id = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_ID]);
    ecnl_table_entry_t *ecnl_table = e_dev->ecnl_tables[id];
    int t_size = e_dev->ecnl_tables_size[id];

    u32 size = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_CONTENT_SIZE]);

    u32 location = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_LOCATION]);
    if (location + size > t_size) return -EINVAL;

    char *p = (char *) &ecnl_table[location];
    nla_memcpy(p, info->attrs[NL_ECNL_ATTR_TABLE_CONTENT], sizeof(struct ecnl_table_entry) * size);
    // memcpy(p, (char*) nla_data(info->attrs[NL_ECNL_ATTR_TABLE_CONTENT]), sizeof(struct ecnl_table_entry) * size); // nla_get_unspec, nla_len

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_FILL_TABLE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_fill_table_entry(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_TABLE_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_TABLE_ENTRY_LOCATION]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_TABLE_ENTRY]) return -EINVAL;

    u32 id = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_ID]);
    ecnl_table_entry_t *ecnl_table = e_dev->ecnl_tables[id];
    int t_size = e_dev->ecnl_tables_size[id];

    u32 location = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_ENTRY_LOCATION]);
    if (location > t_size) return -EINVAL;

    ecnl_table_entry_t *p = &ecnl_table[location];
    nla_memcpy(p, info->attrs[NL_ECNL_ATTR_TABLE_ENTRY], sizeof(struct ecnl_table_entry));
    // char *entry = (char*) nla_data(info->attrs[NL_ECNL_ATTR_TABLE_ENTRY]); // nla_get_unspec, nla_len
    // memcpy((char*)&ecnl_table[location], entry, sizeof(struct ecnl_table_entry));

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_FILL_TABLE_ENTRY);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_select_table(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_TABLE_ID]) return -EINVAL;

    u32 id = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_ID]);
    ecnl_table_entry_t *ecnl_table = e_dev->ecnl_tables[id];
    if (ecnl_table) {
        unsigned long flags;
        spin_lock_irqsave(&e_dev->drivers_lock, flags);
        e_dev->current_table = ecnl_table;
        spin_unlock_irqrestore(&e_dev->drivers_lock, flags);
    }

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_SELECT_TABLE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_dealloc_table(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_TABLE_ID]) return -EINVAL;

    u32 id = nla_get_u32(info->attrs[NL_ECNL_ATTR_TABLE_ID]);
    if (e_dev->ecnl_tables[id]) {
        if (e_dev->current_table == e_dev->ecnl_tables[id]) {
            e_dev->fw_enable = 0;
            e_dev->current_table = NULL;
        }
        kfree(e_dev->ecnl_tables[id]);
        e_dev->ecnl_tables[id] = NULL;
    }
    e_dev->ecnl_tables_size[id] = 0;

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_DEALLOC_TABLE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_TABLE_ID, id));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

// FIXME:
static int nl_ecnl_map_ports(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

// FIXME: harden this - mp length (ENCL_FW_TABLE_ENTRY_ARRAY)
    if (info->attrs[NL_ECNL_ATTR_TABLE_MAP]) {
        // char *p = &e_dev->fw_map[0];
        // nla_memcpy(e_dev->fw_map, info->attrs[NL_ECNL_ATTR_TABLE_MAP], sizeof(u32) * ENCL_FW_TABLE_ENTRY_ARRAY);
        u32 *mp = (u32 *) nla_data(info->attrs[NL_ECNL_ATTR_TABLE_MAP]); // nla_get_unspec, nla_len
        for (int i = 0; i < ENCL_FW_TABLE_ENTRY_ARRAY; i++) {
            e_dev->fw_map[i] = mp[i];
        }
    }

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_MAP_PORTS);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

// UNUSED ??
static struct entl_driver * find_driver(struct ecnl_device* e_dev, char *name) {
    for (int i = 0; i < e_dev->num_ports; i++) {
        struct entl_driver *e_driver = &e_dev->drivers[i];
        if (strcmp(e_driver->name, name) == 0) return e_driver;
    }
    return NULL;
}

static int nl_ecnl_start_forwarding(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    e_dev->fw_enable = true;

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_START_FORWARDING);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_stop_forwarding(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    e_dev->fw_enable = false;

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_STOP_FORWARDING);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_send_ait_message(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_MESSAGE]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);

    struct ec_ait_data ait_data; memset(&ait_data, 0, sizeof(struct ec_ait_data));
    ait_data.message_len = nla_get_u32(info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH]);
    nla_memcpy(ait_data.data, info->attrs[NL_ECNL_ATTR_MESSAGE], ait_data.message_len);
    // memcpy(ait_data.data, (char*)nla_data(info->attrs[NL_ECNL_ATTR_MESSAGE]), ait_data.message_len); // nla_get_unspec, nla_len

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return -EINVAL;

    funcs->send_AIT_message((struct sk_buff *) &ait_data, e1000e);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_SEND_AIT_MESSAGE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id));
    //NLAPUT_CHECKED(nla_put(rskb, NL_ECNL_ATTR_AIT_MESSAGE, sizeof(struct entt_ioctl_ait_data), ait_data));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_retrieve_ait_message(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_NO]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);

    struct ec_alo_reg alo_reg; memset(&alo_reg, 0, sizeof(struct ec_alo_reg));
    alo_reg.reg = nla_get_u64(info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]);
    alo_reg.index = nla_get_u32(info->attrs[NL_ECNL_ATTR_ALO_REG_NO]);

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return -EINVAL;

    struct ec_ait_data ait_data; memset(&ait_data, 0, sizeof(struct ec_ait_data));
    funcs->retrieve_AIT_message(e1000e, &ait_data);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MESSAGE_LENGTH, ait_data.message_len));
    NLAPUT_CHECKED(nla_put(rskb, NL_ECNL_ATTR_MESSAGE, ait_data.message_len, ait_data.data));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_write_alo_register(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    struct nlattr *na;
    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_NO]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);

    struct ec_alo_reg alo_reg; memset(&alo_reg, 0, sizeof(struct ec_alo_reg));
    alo_reg.reg = nla_get_u64(info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]);
    alo_reg.index = nla_get_u32(info->attrs[NL_ECNL_ATTR_ALO_REG_NO]);

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return -EINVAL;

    funcs->write_alo_reg(e1000e, &alo_reg);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_WRITE_ALO_REGISTER);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_read_alo_registers(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_ALO_REG_NO]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);

    struct ec_alo_reg alo_reg; memset(&alo_reg, 0, sizeof(struct ec_alo_reg));
    alo_reg.reg = nla_get_u64(info->attrs[NL_ECNL_ATTR_ALO_REG_DATA]);
    alo_reg.index = nla_get_u32(info->attrs[NL_ECNL_ATTR_ALO_REG_NO]);

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return -EINVAL;

    struct ec_alo_regs alo_regs; memset(&alo_regs, 0, sizeof(struct ec_alo_regs));
    funcs->read_alo_regs(e1000e, &alo_regs);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, info->snd_portid, info->snd_seq, &nl_ecnd_fam, 0, NL_ECNL_CMD_READ_ALO_REGISTERS);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, e_dev->index)); // module_id
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, port_id));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_ALO_FLAG, alo_regs.flags));
    NLAPUT_CHECKED(nla_put(rskb, NL_ECNL_ATTR_ALO_REG_VALUES, sizeof(uint64_t)*32, alo_regs.regs));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_reply(rskb, info);
}

static int nl_ecnl_send_discover_message(struct sk_buff *skb, struct genl_info *info) {
    struct ecnl_device *e_dev = fetch_ecnl_device(info);
    if (!e_dev) return -ENODEV;

    if (!info->attrs[NL_ECNL_ATTR_PORT_ID]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_DISCOVERING_MSG]) return -EINVAL;
    if (!info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH]) return -EINVAL;

    u32 port_id = nla_get_u32(info->attrs[NL_ECNL_ATTR_PORT_ID]);

    u32 len = nla_get_u32(info->attrs[NL_ECNL_ATTR_MESSAGE_LENGTH]);
    struct sk_buff *rskb = alloc_skb(len,  GFP_ATOMIC); if (rskb == NULL) return -ENOMEM;
    rskb->len = len;

    nla_memcpy(rskb->data, info->attrs[NL_ECNL_ATTR_DISCOVERING_MSG], len);
    // char *data = (char *) nla_data(info->attrs[NL_ECNL_ATTR_DISCOVERING_MSG]); // nla_get_unspec, nla_len
    // memcpy(rskb->data, data, len);

    struct entl_driver *e_driver = &e_dev->drivers[port_id];
    if (!e_driver) return -EINVAL;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return -EINVAL;

    funcs->start_xmit(rskb, e1000e);
    return -1; // FIXME
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
        .cmd = NL_ECNL_CMD_SIGNAL_AIT_MESSAGE,
        .doit = nl_ecnl_send_ait_message,  // dummy func
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

// unused ?
static int ecnl_driver_index(unsigned char *ecnl_name) {
    struct net_device *plug_in = find_ecnl_device(ecnl_name);
    if (plug_in == NULL) {
        ECNL_DEBUG("ecnl_driver_index module \"%s\" not found\n", ecnl_name);
        return -EINVAL;
    }

    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    return e_dev->index; // module_id
}

static int ecnl_register_port(int module_id, unsigned char *name, struct net_device *e1000e, struct entl_driver_funcs *funcs) {
    struct net_device *plug_in = ecnl_devices[module_id];
    if (plug_in == NULL) {
        ECNL_DEBUG("ecnl_register_port module-id %d not found\n", module_id);
        return -EINVAL;
    }

    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    int port_id = -1;
    unsigned long flags;
    spin_lock_irqsave(&e_dev->drivers_lock, flags);
    // ECNL_DEBUG("ecnl_register_port \"%s\" port-id %d\n", name, e_dev->num_ports);
    if (e_dev->num_ports < ECNL_DRIVER_MAX) {
        // FIXME: ENCL_FW_TABLE_ENTRY_ARRAY
        port_id = e_dev->num_ports++;

        e_dev->fw_map[port_id] = port_id; // default map by register order

        ECNL_DEBUG("ecnl_register_port module-id %d port-name \"%s\" port-id %d\n", module_id, name, port_id);
        struct entl_driver *e_driver = &e_dev->drivers[port_id];
        e_driver->index = port_id; // port_id
        e_driver->name = name;
        e_driver->device = e1000e;
        e_driver->funcs = funcs;
    }
    else {
        ECNL_DEBUG("ecnl_register_port module-id %d table overflow %d\n", module_id, e_dev->num_ports);
    }
    spin_unlock_irqrestore(&e_dev->drivers_lock, flags);

    return port_id;
}

static void ecnl_deregister_ports(int module_id) {
    struct net_device *plug_in = ecnl_devices[module_id];
    if (plug_in == NULL) {
        ECNL_DEBUG("ecnl_deregister_ports module-id %d not found\n", module_id);
        return;
    }

    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    unsigned long flags;
    spin_lock_irqsave(&e_dev->drivers_lock, flags);
    e_dev->num_ports = 0;
    spin_unlock_irqrestore(&e_dev->drivers_lock, flags);
}

/*
static int ecnl_hard_header(struct sk_buff *skb, struct net_device *plug_in, unsigned short type, void *daddr, void *saddr, unsigned len) {
    return 0;
}

static int ecnl_rebuild_header(struct sk_buff *skb) {
    return 0;
}

static u8 ecnl_table_lookup(struct ecnl_device *e_dev, u16 u_addr, u32 l_addr) {
    if (e_dev->hash_enable) {
        u16 hash_entry = addr_hash_10(u_addr, l_addr);
        while (1) {
            struct ecnl_hash_entry *h_e = &e_dev->current_hash_table[hash_entry];
            if (h_e->u_addr == u_addr && h_e->l_addr == l_addr) {
                u32 offset = h_e->location & 0xf;
                ecnl_table_entry e =  e_dev->current_table[h_e->location >> 4];
                return (e >> offset) & 0xf;
            }
            if (h_e->next == 0) return 0xff; // not found
            hash_entry = h_e->next;
        }
    }
    else {
        u32 offset = l_addr & 0xf;
        ecnl_table_entry e = e_dev->current_table[l_addr>>4];
        return (e >> offset) & 0xf;
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

static void set_next_id(struct sk_buff *skb, u32 nextID) {
    struct ethhdr *eth = (struct ethhdr *) skb->data;
    eth->h_source[2] = 0xff & (nextID >> 24);
    eth->h_source[3] = 0xff & (nextID >> 16);
    eth->h_source[4] = 0xff & (nextID >>  8);
    eth->h_source[5] = 0xff & (nextID);
}

static int ecnl_receive_skb(int module_id, int index, struct sk_buff *skb) {
    struct net_device *plug_in = ecnl_devices[module_id];
    if (plug_in == NULL) {
        ECNL_DEBUG("ecnl_receive_skb module-id %d not found\n", module_id);
        return -EINVAL;
    }

    struct ethhdr *eth = (struct ethhdr *) skb->data;

    // no forwarding, send to host
    u8 dest_fw = eth->h_dest[0] & 0x80;
    if (dest_fw == 0) {
        netif_rx(skb);
        return 0;
    }

    // forwarding disabled, send to host
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    if (!e_dev->fw_enable) {
        netif_rx(skb);
        return 0;
    }

    u8 host_on_backward = eth->h_source[0] & 0x40;
    u8 direction =        eth->h_source[0] & 0x80;
    u32 id =  (u32) eth->h_source[2] << 24
            | (u32) eth->h_source[3] << 16
            | (u32) eth->h_source[4] << 8
            | (u32) eth->h_source[5];

    // table miss, send to host
    if (!e_dev->current_table || id >= e_dev->current_table_size) {
        ECNL_DEBUG("ecnl_receive_skb module-id %d can't forward packet id %d\n", module_id, id);
        netif_rx(skb);
        return 0;
    }

    ecnl_table_entry_t entry; // FIXME
    unsigned long flags;
    spin_lock_irqsave(&e_dev->drivers_lock, flags);
    memcpy(&entry, &e_dev->current_table[id], sizeof(ecnl_table_entry_t));
    spin_unlock_irqrestore(&e_dev->drivers_lock, flags);

    u16 port_vector = entry.info.port_vector;
    if (direction == 0) {  // forward direction
        if (port_vector == 0) {
            ECNL_DEBUG("ecnl_receive_skb no forward bit, module-id %d %08x\n", module_id, index);
            return -EINVAL;
        }

        // send to this host
        if (port_vector & 1) {
            if (port_vector & 0xfffe) {
                struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                netif_rx(skbc);
            }
            else netif_rx(skb);
        }

        // multi-port forwarding
        port_vector &= ~(u16)(1 << index); // avoid to send own port
        port_vector = (port_vector >> 1);  // reduce host bit

        for (int i = 0; i < ENCL_FW_TABLE_ENTRY_ARRAY; i++) {
            if (port_vector & 1) {
                int id = e_dev->fw_map[i];
                u32 nextID = entry.nextID[id];

                // FIXME : error or warning ??
                struct entl_driver *e_driver = &e_dev->drivers[id];
                if (!e_driver) continue;
                struct net_device *e1000e = e_driver->device;
                struct entl_driver_funcs *funcs = e_driver->funcs;
                if (!e1000e || !funcs) continue;

                // forwarding to next e_driver
                if (port_vector & 0xfffe) {
                    struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                    set_next_id(skbc, nextID);
                    funcs->start_xmit(skbc, e1000e);
                }
                else {
                    set_next_id(skb, nextID);
                    funcs->start_xmit(skb, e1000e);
                }
            }
            port_vector = port_vector >> 1;
        }
    }
    else { // backward
        u8 parent = entry.info.parent;
        // send to this host
        if (parent == 0 || host_on_backward) {
            if (parent > 0) {
                struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                netif_rx(skbc);
            }
            else netif_rx(skb);
        }
// FIXME: harden against ENCL_FW_TABLE_ENTRY_ARRAY ??
        if (parent > 0) {
            int id = e_dev->fw_map[parent];
            u32 nextID = entry.nextID[id];
            set_next_id(skb, nextID);

            struct entl_driver *e_driver = &e_dev->drivers[id];
            if (!e_driver) return -EINVAL;
            struct net_device *e1000e = e_driver->device;
            struct entl_driver_funcs *funcs = e_driver->funcs;
            if (!e1000e || !funcs) return -EINVAL;

            funcs->start_xmit(skb, e1000e);
        }
    }

    return 0;
}


// PUB/SUB section:


// entl e_driver received a discovery message
static int ecnl_receive_dsc(int module_id, int index, struct sk_buff *skb) {
    struct ethhdr *eth = (struct ethhdr *)skb->data;
    struct net_device *plug_in = ecnl_devices[module_id];
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, module_id));
    NLAPUT_CHECKED(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index));
    NLAPUT_CHECKED(nla_put(rskb, NL_ECNL_ATTR_DISCOVERING_MSG, skb->len, skb->data));
    genlmsg_end(rskb, user_hdr);
    return genlmsg_multicast_allns(&nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_DISCOVERY, GFP_KERNEL);
}

// entl e_driver has a link update
static void ecnl_link_status_update(int module_id, int index, struct ec_state *state) {
    struct net_device *plug_in = ecnl_devices[module_id];
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    struct entl_driver *e_driver = &e_dev->drivers[index];

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return; // -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_GET_PORT_STATE);
    NLAPUT_CHECKED_ZZ(add_link_state(rskb, e_dev, e_driver, state));
    genlmsg_end(rskb, user_hdr);
    genlmsg_multicast_allns(&nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_LINKSTATUS, GFP_KERNEL);
}

static void ecnl_forward_ait_message(int module_id, int drv_index, struct sk_buff *skb) {
    struct net_device *plug_in = ecnl_devices[module_id];
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    struct ethhdr *eth = (struct ethhdr *)skb->data;
    u32 alo_command = (uint32_t) eth->h_dest[2] << 8
                    | (uint32_t) eth->h_dest[3];

    u8 to_host;
    if ((eth->h_dest[0] & 0x80) != 0) {  // fw bit set
        u8 host_on_backward = eth->h_source[0] & 0x40;
        u8 direction =        eth->h_source[0] & 0x80;
        u32 id =  (u32) eth->h_source[2] << 24
                | (u32) eth->h_source[3] << 16
                | (u32) eth->h_source[4] <<  8
                | (u32) eth->h_source[5];

        if (e_dev->fw_enable
        &&  e_dev->current_table && id < e_dev->current_table_size) {
            u16 port_vector;

            ecnl_table_entry_t entry; // FIXME
            unsigned long flags;
            spin_lock_irqsave(&e_dev->drivers_lock, flags);
            memcpy(&entry, &e_dev->current_table[id], sizeof(ecnl_table_entry_t));
            spin_unlock_irqrestore(&e_dev->drivers_lock, flags);

            port_vector = entry.info.port_vector;
            if (direction == 0) {  // forward direction
                if (port_vector == 0) {
                    ECNL_DEBUG("ecnl_forward_ait_message no forward bit module-id %d xx %08x\n", module_id, drv_index);
                    to_host = 1;				
                }
                else {
                    if (port_vector & 1) to_host = 1;
                    port_vector &= ~(u16)(1 << drv_index); // avoid to send own port
                    port_vector >>= 1; // reduce host bit

                    // device-index, NOT port-index!!
                    for (int i = 0; i < ENCL_FW_TABLE_ENTRY_ARRAY && port_vector > 0; i++, port_vector >>= 1) {
                        if ((port_vector & 1) == 0) continue;

                        int id = e_dev->fw_map[i];
                        u32 nextID = entry.nextID[id];

                        // FIXME : error or warning ??
                        struct entl_driver *e_driver = &e_dev->drivers[id];
                        if (!e_driver) continue;
                        struct net_device *e1000e = e_driver->device;
                        struct entl_driver_funcs *funcs = e_driver->funcs;
                        if (!e1000e || !funcs) continue;

                        struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                        set_next_id(skbc, nextID);
                        funcs->send_AIT_message(skbc, e1000e);
                    }
                }
            }
            else {
// FIXME: harden against ENCL_FW_TABLE_ENTRY_ARRAY ??
                // backword transfer
                u8 parent = entry.info.parent;
                if (parent == 0 || host_on_backward) {
                    to_host = 1;
                }
                int module_id = e_dev->index;
                if (parent > 0 && module_id != parent) {
                    int id = e_dev->fw_map[parent];
                    u32 nextID = entry.nextID[id];

                    struct entl_driver *e_driver = &e_dev->drivers[id];
                    if (!e_driver) return; // -1;
                    struct net_device *e1000e = e_driver->device;
                    struct entl_driver_funcs *funcs = e_driver->funcs;
                    if (!e1000e || !funcs) return; // -1;

                    struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                    set_next_id(skbc, nextID);
                    funcs->send_AIT_message(skbc, e1000e);
                }
            }
        }
    }

    if (to_host && alo_command == 0) {  // do not forward ALO operation message
        struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
        if (!rskb) return; // -ENOMEM;

        void *user_hdr = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_RETRIEVE_AIT_MESSAGE);
        NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, module_id));
        NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, drv_index));
        NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_MESSAGE_LENGTH, skb->len));
        NLAPUT_CHECKED_ZZ(nla_put(rskb, NL_ECNL_ATTR_MESSAGE, skb->len, skb->data));
        genlmsg_end(rskb, user_hdr);
        genlmsg_multicast_allns(&nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL);
        return;
    }

    return; // -1;
}

static void ecnl_got_ait_message(int module_id, int drv_index, int num_message) {
    //struct ethhdr *eth = (struct ethhdr *)skb->data;
    struct net_device *plug_in = ecnl_devices[module_id];
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    struct entl_driver *e_driver = &e_dev->drivers[drv_index];

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return; // -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_SIGNAL_AIT_MESSAGE);
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, module_id));
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, drv_index));
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_NUM_AIT_MESSAGES, num_message));
    genlmsg_end(rskb, user_hdr);
    genlmsg_multicast_allns(&nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL);
}

static void ecnl_got_alo_update(int module_id, int index) {
    //struct ethhdr *eth = (struct ethhdr *)skb->data;
    struct net_device *plug_in = ecnl_devices[module_id];
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    struct ec_alo_regs alo_regs; memset(&alo_regs, 0, sizeof(struct ec_alo_regs));

    struct entl_driver *e_driver = &e_dev->drivers[index];
    if (!e_driver) return; // -1;
    struct net_device *e1000e = e_driver->device;
    struct entl_driver_funcs *funcs = e_driver->funcs;
    if (!e1000e || !funcs) return; // -1;

    funcs->read_alo_regs(e1000e, &alo_regs);

    struct sk_buff *rskb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!rskb) return; // -ENOMEM;

    void *user_hdr = genlmsg_put(rskb, 0, 0, &nl_ecnd_fam, 0, NL_ECNL_CMD_READ_ALO_REGISTERS);
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_MODULE_ID, module_id));
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_PORT_ID, index));
    NLAPUT_CHECKED_ZZ(nla_put(rskb, NL_ECNL_ATTR_ALO_REG_VALUES, sizeof(uint64_t)*32, alo_regs.regs));
    NLAPUT_CHECKED_ZZ(nla_put_u32(rskb, NL_ECNL_ATTR_ALO_FLAG, alo_regs.flags));
    genlmsg_end(rskb, user_hdr);
    genlmsg_multicast_allns(&nl_ecnd_fam, rskb, 0, NL_ECNL_MCGRP_AIT, GFP_KERNEL);
}

static struct ecnl_device_funcs ecnl_api_funcs = {
    .register_port = ecnl_register_port,
    .deregister_ports = ecnl_deregister_ports,
    .receive_skb = ecnl_receive_skb,
    //.receive_dsc = ecnl_receive_dsc,
    .link_status_update = ecnl_link_status_update,
    .forward_ait_message = ecnl_forward_ait_message,
    .got_ait_massage = ecnl_got_ait_message,
    .got_alo_update = ecnl_got_alo_update
};

EXPORT_SYMBOL(ecnl_api_funcs);

// net_device interface functions
static int ecnl_open(struct net_device *plug_in) {
    ECNL_DEBUG("ecnl_open \"%s\"\n", plug_in->name);
    netif_start_queue(plug_in);
    return 0;
}

static int ecnl_stop(struct net_device *plug_in) {
    ECNL_DEBUG("ecnl_stop \"%s\"\n", plug_in->name);
    netif_stop_queue(plug_in);
    return 0;
}

static int ecnl_hard_start_xmit(struct sk_buff *skb, struct net_device *plug_in) {
    struct ethhdr *eth = (struct ethhdr *) skb->data;
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);

    if (!e_dev->fw_enable) return -EINVAL;

    u8 direction =  eth->h_source[0] & 0x80;
    u32 id =  (u32) eth->h_source[2] << 24
            | (u32) eth->h_source[3] << 16
            | (u32) eth->h_source[4] <<  8
            | (u32) eth->h_source[5];

    // FIXME: direct mapped table ??

    if (!e_dev->current_table || id >= e_dev->current_table_size) {
        ECNL_DEBUG("ecnl_hard_start_xmit \"%s\" can't forward packet\n", e_dev->name);
        return -EINVAL;
    }

    ecnl_table_entry_t entry; // FIXME
    unsigned long flags;
    spin_lock_irqsave(&e_dev->drivers_lock, flags);
    memcpy(&entry, &e_dev->current_table[id], sizeof(ecnl_table_entry_t));
    spin_unlock_irqrestore(&e_dev->drivers_lock, flags);

    u16 port_vector = entry.info.port_vector;
    if (direction == 0) {  // forward direction
        port_vector >>= 1; // remove host bit

        for (int i = 0; i < ENCL_FW_TABLE_ENTRY_ARRAY; i++) {
            if (port_vector & 1) {
                int id = e_dev->fw_map[i];
                u32 nextID = entry.nextID[id];

                struct entl_driver *e_driver = &e_dev->drivers[id];
                if (!e_driver) return -EINVAL;
                struct net_device *e1000e = e_driver->device;
                struct entl_driver_funcs *funcs = e_driver->funcs;
                if (!e1000e || !funcs) return -EINVAL;

                // forwarding to next e_driver
                if (port_vector & 0xfffe) {
                    struct sk_buff *skbc = skb_clone(skb, GFP_ATOMIC);
                    set_next_id(skbc, nextID);
                    funcs->start_xmit(skbc, e1000e);
                }
                else {
                    set_next_id(skb, nextID);
                    funcs->start_xmit(skb, e1000e);
                }
            }
            port_vector = (port_vector >> 1);
        }
    }
    else {  // to parent side
        u8 parent = entry.info.parent;
        if (parent == 0) {
            // send to this host
            ECNL_DEBUG("ecnl_hard_start_xmit \"%s\" forwarding packet to self\n", e_dev->name);
            return -EINVAL;
        }
        else {
// FIXME: harden against ENCL_FW_TABLE_ENTRY_ARRAY ??
            int id = e_dev->fw_map[parent];
            u32 nextID = entry.nextID[id];
            set_next_id(skb, nextID);

            struct entl_driver *e_driver = &e_dev->drivers[id];
            if (!e_driver) return -EINVAL;
            struct net_device *e1000e = e_driver->device;
            struct entl_driver_funcs *funcs = e_driver->funcs;
            if (!e1000e || !funcs) return -EINVAL;

            // forwarding to next e_driver
            funcs->start_xmit(skb, e1000e);
        }
    }

    return 0;
}

static void ecnl_tx_timeout(struct net_device *plug_in) {
    // return 0;
}

static struct net_device_stats *ecnl_get_stats(struct net_device *plug_in) {
    struct ecnl_device *e_dev = (struct ecnl_device *) netdev_priv(plug_in);
    return &e_dev->net_stats;
}

static struct net_device_ops ecnl_netdev_ops = {
    .ndo_open = ecnl_open,
    .ndo_stop = ecnl_stop,
    .ndo_start_xmit = ecnl_hard_start_xmit,
    .ndo_tx_timeout = ecnl_tx_timeout,
    .ndo_get_stats = ecnl_get_stats
};

// --

// ref: linux/netdevice.h - enum netdev_tx
netdev_tx_t adapt_start_xmit(struct sk_buff *skb, struct net_device *e1000e) { return NETDEV_TX_BUSY; }
int adapt_send_AIT_message(struct sk_buff *skb, struct net_device *e1000e) { return -1; }
int adapt_retrieve_AIT_message(struct net_device *e1000e, ec_ait_data_t *data) { return -1; }
int adapt_write_alo_reg(struct net_device *e1000e, ec_alo_reg_t *reg) { return -1; }
int adapt_read_alo_regs(struct net_device *e1000e, ec_alo_regs_t *regs) { return -1; }

#if 0
// The data structre represents the internal state of ENTL
typedef struct ec_state {
    uint64_t recover_count; // how many recover happened
    uint64_t recovered_count; // how many recovered happened
    uint64_t s_count; // how many s message happened
    uint64_t r_count; // how many r message happened
    uint64_t entt_count; // how many entt transaction happened
    uint64_t aop_count; // how many aop transaction happened
    int link_state; // link state
    int num_queued; // num AIT messages
    struct timespec update_time; // last updated time in microsecond resolution
#ifdef ENTL_SPEED_CHECK
    struct timespec interval_time; // the last interval time between S <-> R transition
    struct timespec max_interval_time; // the max interval time
    struct timespec min_interval_time; // the min interval time
#endif
} ec_state_t;

typedef struct {
    char *module_name;
    char *port_name;
    uint32_t port_link_state;
    uint64_t port_s_counter;
    uint64_t port_r_counter;
    uint64_t port_recover_counter;
    uint64_t port_recovered_counter;
    uint64_t port_entt_count;
    uint64_t port_aop_count;
    uint32_t num_ait_messages;
} link_state_t;

typedef struct entl_device {
    entl_state_machine_t stm;
    struct timer_list watchdog_timer;
    struct work_struct watchdog_task;
    int user_pid; // subscribed listener
    __u32 flag; // used to set a request to the service task
    __u16 u_addr; __u32 l_addr; // last value - for retry
    int action;
    char name[ENTL_DEVICE_NAME_LEN];
    ENTL_skb_queue_t tx_skb_queue;
    int queue_stopped;
} entl_device_t;

#endif

// ref: <ec_user_api.h>, e1000e-3.3.4/src/entl_device.c
int adapt_get_entl_state(struct net_device *e1000e, ec_state_t *state) {
    if (state == NULL) return -1;

    // ref: e1000e-3.3.4/src/e1000.h
    struct e1000_adapter *adapter = netdev_priv(e1000e);
    // printk(KERN_INFO "net_device: \"%s\", adapter: %p\n", e1000e->name, adapter);
    if (adapter == NULL) return -1;

    entl_device_t *entl_dev = &adapter->entl_dev;
    // printk(KERN_INFO "entl_dev: %p\n", entl_dev);
    if (entl_dev == NULL) return -1;

    printk(KERN_INFO "adapt_get_entl_state e1000e \"%s\"\n", e1000e->name);
    printk(KERN_INFO "  entl_dev->name: \"%s\"\n", entl_dev->name);
    printk(KERN_INFO "  entl_dev->queue_stopped: %d\n", entl_dev->queue_stopped);

    char *nic_name = adapter->netdev->name;
    if (netif_carrier_ok(e1000e)) {
        int link_speed = adapter->link_speed;
        int link_duplex = adapter->link_duplex;
        printk(KERN_INFO "\"%s\" NIC Link is Up %d Mbps %s Duplex\n", nic_name, link_speed, (link_duplex == FULL_DUPLEX) ? "Full" : "Half");
    }
    else {
        printk(KERN_INFO "\"%s\" NIC Link is Down\n", nic_name);
    }

#if 0
    // ref: add_link_state
    state->link_state = link_state;
    state->s_count = s_count;
    state->r_count = r_count;
    state->recover_count = recover_count;
    state->recovered_count = recovered_count;
    state->entt_count = entt_count;
    state->aop_count = aop_count;
    state->num_queued = num_queued;
    // FIXME: update_time, interval_time, max_interval_time, min_interval_time
#endif
    return 0;
}

// ref: ecnl_device.h
static struct entl_driver_funcs adapt_funcs = {
    .start_xmit = &adapt_start_xmit,
    .send_AIT_message = &adapt_send_AIT_message,
    .get_entl_state = &adapt_get_entl_state,
    .retrieve_AIT_message = &adapt_retrieve_AIT_message,
    .write_alo_reg = &adapt_write_alo_reg,
    .read_alo_regs = &adapt_read_alo_regs,
};

typedef struct {
    int index;
    unsigned char *name;
    struct net_device *e1000e;
} e1000e_hackery_t;

// #define ARRAY_SIZE(X) (sizeof(X) / sizeof((X)[0])) // <linux/kernel.h>

e1000e_hackery_t e1000e_ports[] = {
    { .index = 1, .name = "enp6s0", .e1000e = NULL, },
    { .index = 2, .name = "enp8s0", .e1000e = NULL, },
    { .index = 3, .name = "enp9s0", .e1000e = NULL, },
    { .index = 4, .name = "enp7s0", .e1000e = NULL, },
};

// FIXME : should instead auto-detect compatible instances
static void inject_dev(struct net_device *n_dev) {
    for (int i = 0; i < ARRAY_SIZE(e1000e_ports); i++) {
        e1000e_hackery_t *p = &e1000e_ports[i];
        if (strcmp(n_dev->name, p->name) == 0) { p->e1000e = n_dev; } // inject reference
    }
}

static void hack_init(void) {
    int module_id = 0; // ecnl0
    struct entl_driver_funcs *funcs = &adapt_funcs;
    for (int i = 0; i < ARRAY_SIZE(e1000e_ports); i++) {
        e1000e_hackery_t *p = &e1000e_ports[i];
        int port_no = ecnl_register_port(module_id, p->name, p->e1000e, funcs);
        if (port_no < 0) { printk(KERN_INFO "failed to register \"%s\"\n", p->name); }
    }
}

// Q: what locking is required here?
// /Users/bjackson/git-projects/ubuntu-bionic/include/linux/netdevice.h
extern struct net init_net;
static void scan_netdev(void) {
    read_lock(&dev_base_lock);
    const struct net *net = &init_net;
    struct net_device *n_dev; for_each_netdev(net, n_dev) {
    // for (struct net_device *n_dev = first_net_device(net); n_dev; n_dev = next_net_device(n_dev)) {
        ECNL_DEBUG("scan_netdev considering [%s]\n", n_dev->name);
        inject_dev(n_dev);
    }
    read_unlock(&dev_base_lock);
}

// --

static void ecnl_setup(struct net_device *plug_in) {
    plug_in->netdev_ops = &ecnl_netdev_ops;
    plug_in->flags |= IFF_NOARP;
}

static int __init ecnl_init_module(void) {
    if (device_busy) {
        ECNL_DEBUG("ecnl_init_module called on busy state\n");
        return -EINVAL;
    }

    pr_info("Earth Computing Generic Netlink Module - %s ", ECNL_DEVICE_DRIVER_VERSION);
    pr_info("Copyright(c) 2018, 2019 Earth Computing\n");

    int err = genl_register_family_with_ops_groups(&nl_ecnd_fam, nl_ecnl_ops, nl_ecnd_mcgrps);
    if (err) {
        ECNL_DEBUG("ecnl_init_module failed register genetlink family: \"%s\"\n", nl_ecnd_fam.name);
        return -EINVAL;
    }

    ECNL_DEBUG("registered genetlink family: \"%s\"\n", nl_ecnd_fam.name);        	

    struct net_device *plug_in = alloc_netdev(sizeof(struct ecnl_device), MAIN_DRIVER_NAME, NET_NAME_UNKNOWN, ecnl_setup);
    struct ecnl_device *this_device = (struct ecnl_device *) netdev_priv(plug_in);
    memset(this_device, 0, sizeof(struct ecnl_device));
    strcpy(this_device->name, MAIN_DRIVER_NAME);
    this_device->index = 0;

    spin_lock_init(&this_device->drivers_lock);
    device_busy = 1;

    //inter_module_register("ecnl_driver_funcs", THIS_MODULE, ecnl_funcs);

    if (register_netdev(plug_in)) {
        ECNL_DEBUG("ecnl_init_module failed register net_dev: \"%s\"\n", this_device->name);
    }

    ecnl_devices[num_ecnl_devices++] = plug_in;

    scan_netdev();
    hack_init();
    return 0;
}

// FIXME: clean up data
static void __exit ecnl_cleanup_module(void) {
    if (device_busy) {
        ECNL_DEBUG("ecnl_cleanup_module busy\n");
        //inter_module_unregister("ecnl_driver_funcs");
        device_busy = 0;
    }
    else {
        ECNL_DEBUG("ecnl_cleanup_module non-busy\n");		
    }
}

module_init(ecnl_init_module);
module_exit(ecnl_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
MODULE_VERSION(DRV_VERSION);

