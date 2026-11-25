// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * xl_eth.c - Ethernet over CAN XL (CiA 611-1 SDT 04h/05h)
 *
 * The canxl_eth netdevice is an Ethernet interface stacked on top of a
 * CAN XL network interface. IEEE 802.3 MAC frames (without FCS) are
 * transported in the data field of CAN XL frames with the service data
 * unit type (SDT) 04h (IEEE 802.3 MAC frame) or 05h (IEEE 802.3 MAC frame
 * extended) as specified in CiA 611-1.
 *
 *   ip link add link can0 name xleth0 type canxl_eth sdt 5 \
 *           tx_prio 0x123 rx_prio 0x100 rx_prio_mask 0x700
 *
 * The canxl_eth device is a regular user of the PF_CAN network layer: CAN XL
 * frames are sent with can_send() and received with can_rx_register().
 * Therefore it is independent from the specific CAN XL hardware driver and
 * the CAN XL interface remains fully usable for other CAN traffic.
 *
 * Configuration:
 *
 *   Like the addressing of a CAN_ISOTP socket at bind() time, the CAN XL
 *   settings sdt, tx_prio, rx_prio and rx_prio_mask have to be provided
 *   when creating the canxl_eth device. Only the VCID settings (default:
 *   no VCID) and the flags (default: echo on, SEC = 0, RRS = 0) are
 *   optional. The settings can only be changed while the canxl_eth device
 *   is down.
 *
 * Integration into the PF_CAN network layer:
 *
 *   Each canxl_eth device owns a socket-less PF_CAN struct sock. Before
 *   handing an skb to can_send() it is orphaned from its original owner
 *   (e.g. a TCP socket) and owned by this struct sock. This is required as
 *   the PF_CAN protocols expect skb->sk to be a PF_CAN socket, e.g. for the
 *   echo skb handling (can_skb_set_owner()) and for the detection of
 *   locally generated CAN frames (MSG_DONTROUTE in CAN_RAW, RX_LOCAL in
 *   CAN_BCM). The unique struct sock is also used to identify and drop the
 *   own echoed frames in the canxl_eth receive path.
 *   The struct sock is freed when the last skb referencing it is gone. As
 *   it holds a reference to the canxl_eth module, all canxl_eth devices
 *   have to be removed before the canxl_eth module can be unloaded.
 *
 *   With CANXL_ETH_FLAG_ECHO (default) the sent CAN XL frames are echoed
 *   to the local PF_CAN users. E.g. 'candump -x any' shows the Ethernet
 *   frames as TX frames and CAN XL frames with SDT 04h/05h sent with
 *   'cansend' are received by the canxl_eth device. Note: CAN_RAW sockets
 *   only forward CAN XL frames with a non-zero VCID when
 *   CAN_RAW_XL_VCID_OPTS is enabled with CAN_RAW_XL_VCID_RX_FILTER (e.g.
 *   SDT 05h with VLAN tagged frames).
 *
 * Transmit path:
 *   The CAN XL frame header is pushed in front of the Ethernet frame and
 *   the skb is sent via can_send() (zero-copy). The 11 bit CAN XL priority
 *   identifier (tx_prio) is a property of the canxl_eth device. Like the
 *   CAN identifiers of Classical CAN and CAN FD, the CAN XL priority
 *   identifier has to be unique for each sender on the CAN bus.
 *   SEC/RRS are set according to CANXL_ETH_FLAG_SEC/CANXL_ETH_FLAG_RRS.
 *   SDT 04h: AF = CANXL_ETH_SDT04_AF_PAD, VCID = tx_vcid
 *   SDT 05h: AF = 1st/4th/5th/6th octet of the destination MAC address,
 *            VCID = lower 8 bit of the VID of a C-VLAN/S-VLAN tag (or 0)
 *
 * Receive path:
 *   CAN XL frames with SDT 04h or 05h matching the priority ID and VCID
 *   filters (rx_prio, rx_prio_mask, rx_vcid, rx_vcid_mask) and with SEC/RRS
 *   values matching CANXL_ETH_FLAG_SEC/CANXL_ETH_FLAG_RRS are delivered
 *   to the Ethernet stack. The skb provided by the PF_CAN core is cloned
 *   and the CAN XL frame header is pulled (zero-copy).
 *
 * Out-of-tree build:
 *   This file is the canxl_eth source of the Linux patch with additional
 *   compatibility sections for Linux 6.12 and later. The CANXL_ETH_HAVE_*
 *   defines are created in the Kbuild file by checking the kernel headers.
 *   See compat.h and OUT-OF-TREE.md.
 *
 * Copyright (c) 2026 Oliver Hartkopp <socketcan@hartkopp.net>
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/can.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/can/can-ml.h>
#include <linux/can/xl_eth.h>
#include <net/rtnetlink.h>
#include <net/sock.h>

#include "compat.h"

#define DRV_NAME "canxl_eth"

MODULE_DESCRIPTION("PF_CAN Ethernet over CAN XL (CiA 611-1 SDT 04h/05h)");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);

/* SEC/RRS flags set in sent and checked in received CAN XL frames */
#define CANXL_ETH_FRAME_FLAGS	(CANXL_ETH_FLAG_SEC | CANXL_ETH_FLAG_RRS)
#define CANXL_ETH_FLAGS_MASK	(CANXL_ETH_FRAME_FLAGS | CANXL_ETH_FLAG_ECHO)

/* the Ethernet frame (without FCS) is the content of the CAN XL data field */
#define CANXL_ETH_MAX_MTU	(CANXL_MAX_DLEN - ETH_HLEN)

/* canxl_eth configuration (see linux/can/xl_eth.h) */
struct canxl_eth_cfg {
	u16 tx_prio;
	u16 rx_prio;
	u16 rx_prio_mask;
	u8 sdt;
	u8 tx_vcid;
	u8 rx_vcid;
	u8 rx_vcid_mask;
	u8 flags;
};

struct canxl_eth_priv {
	struct net_device *dev;
	struct net_device *candev;	/* the lower CAN XL netdevice */
	netdevice_tracker dev_tracker;
	struct list_head list;		/* entry in canxl_eth_list */
	struct sock *sk;		/* PF_CAN struct sock (skb owner) */
	struct canxl_eth_cfg cfg;
	canid_t rx_can_id;		/* registered PF_CAN filter */
	canid_t rx_mask;		/* registered PF_CAN filter */
};

/* all canxl_eth netdevices (protected by RTNL) */
static LIST_HEAD(canxl_eth_list);

/* protocol for the socket-less struct sock of the canxl_eth devices */
static struct proto canxl_eth_proto = {
	.name		= "CANXL_ETH",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct sock),
};

static struct rtnl_link_ops canxl_eth_link_ops;

/* the carrier of the canxl_eth device follows the CAN XL interface state */
static void canxl_eth_update_carrier(struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct net_device *candev = priv->candev;

	if (netif_running(candev) && netif_carrier_ok(candev) &&
	    can_cap_enabled(candev, CAN_CAP_XL))
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
}

/*
 * Create the PF_CAN filter from the priority ID and VCID filter settings.
 * The VCID is located in the bits 16..23 of the CAN XL prio element which
 * is matched as can_id in the PF_CAN filters.
 *
 * Without a VCID filter the CAN_EFF_FLAG and CAN_RTR_FLAG are added to the
 * mask: This allows the PF_CAN core to use its single SFF identifier filter
 * list (fast path) for rx_prio_mask == CANXL_PRIO_MASK, which only checks
 * the lower 11 bits (the priority ID) of the CAN XL prio element.
 * With a VCID filter the CAN_EFF_FLAG must not be set in the mask, as the
 * PF_CAN core would reduce the mask to (CAN_SFF_MASK | CAN_EFF_RTR_FLAGS).
 */
static void canxl_eth_rx_filter(const struct canxl_eth_cfg *cfg,
				canid_t *can_id, canid_t *mask)
{
	canid_t prio_mask = cfg->rx_prio_mask;
	canid_t vcid_mask = (canid_t)cfg->rx_vcid_mask << CANXL_VCID_OFFSET;

	if (!vcid_mask) {
		if (prio_mask) {
			*can_id = cfg->rx_prio & prio_mask;
			*mask = prio_mask | CAN_EFF_FLAG | CAN_RTR_FLAG;
		} else {
			/* receive all frames */
			*can_id = 0;
			*mask = 0;
		}
		return;
	}

	*can_id = (cfg->rx_prio & prio_mask) |
		  (((canid_t)cfg->rx_vcid << CANXL_VCID_OFFSET) & vcid_mask);
	*mask = prio_mask | vcid_mask;
}

/* CiA 611-1 SDT 05h: AF = DA octets 1, 4, 5, 6 (MSB first) */
static u32 canxl_eth_af(const struct ethhdr *eth)
{
	const u8 *da = eth->h_dest;

	return (u32)da[0] << 24 | (u32)da[3] << 16 | (u32)da[4] << 8 | da[5];
}

/* CiA 611-1 SDT 05h: VCID = lower 8 bit of the VID of the first VLAN tag */
static canid_t canxl_eth_vcid(const struct sk_buff *skb)
{
	const struct vlan_ethhdr *veth = (struct vlan_ethhdr *)skb->data;

	if (skb->len < VLAN_ETH_HLEN)
		return 0;

	if (veth->h_vlan_proto != htons(ETH_P_8021Q) &&
	    veth->h_vlan_proto != htons(ETH_P_8021AD))
		return 0;

	return (ntohs(veth->h_vlan_TCI) & VLAN_VID_MASK) & CANXL_VCID_VAL_MASK;
}

static netdev_tx_t canxl_eth_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct net_device *candev = priv->candev;
	u8 sdt = priv->cfg.sdt;
	canid_t prio = priv->cfg.tx_prio;
	unsigned int len = skb->len;
#ifdef CANXL_ETH_HAVE_CAN_SKB_EXT
	struct can_skb_ext *csx;
#endif
	struct canxl_frame *cxl;
	u32 af;

	/* the Ethernet frame has to fit into the CAN XL data field */
	if (unlikely(len < ETH_HLEN || len > CANXL_MAX_DLEN))
		goto drop;

	/* no sending on a CAN device in read-only mode */
	if (unlikely(can_cap_enabled(candev, CAN_CAP_RO)))
		goto drop;

#ifdef CANXL_ETH_HAVE_CAN_SKB_EXT
	/* CAN drivers expect the entire CAN XL frame in the linear data */
	if (skb_linearize(skb) || skb_cow_head(skb, CANXL_HDR_SIZE))
		goto drop;
#else
	/*
	 * CAN drivers expect the entire CAN XL frame in the linear data.
	 * Without SKB_EXT_CAN the struct can_skb_priv is located at skb->head
	 * which needs additional headroom in front of the CAN XL frame.
	 */
	if (skb_linearize(skb) ||
	    skb_cow_head(skb, CANXL_HDR_SIZE + sizeof(struct can_skb_priv)))
		goto drop;
#endif

	if (sdt == CAN_CIA_ETH_EXT_SDT) {
		af = canxl_eth_af((struct ethhdr *)skb->data);
		prio |= canxl_eth_vcid(skb) << CANXL_VCID_OFFSET;
	} else {
		af = CANXL_ETH_SDT04_AF_PAD;
		prio |= (canid_t)priv->cfg.tx_vcid << CANXL_VCID_OFFSET;
	}

	cxl = skb_push(skb, CANXL_HDR_SIZE);
	cxl->prio = prio;
	cxl->flags = CANXL_XLF | (priv->cfg.flags & CANXL_ETH_FRAME_FLAGS);
	cxl->sdt = sdt;
	cxl->len = len;
	cxl->af = af;

#ifdef CANXL_ETH_HAVE_CAN_SKB_EXT
	csx = can_skb_ext_add(skb);
	if (!csx)
		goto drop;

	csx->can_iif = candev->ifindex;
	csx->can_framelen = 0;
	csx->can_ext_flags = 0;
#else
	/* CAN specific skb content in struct can_skb_priv (no SKB_EXT_CAN) */
	can_skb_prv(skb)->ifindex = candev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;
	can_skb_prv(skb)->frame_len = 0;
#endif

	/* the skb has to be owned by a PF_CAN socket (see top of file) */
	skb_orphan(skb);
	can_skb_set_owner(skb, priv->sk);
	skb->dev = candev;

	/* can_send() consumes the skb in any case */
	if (can_send(skb, !!(priv->cfg.flags & CANXL_ETH_FLAG_ECHO)))
		goto dropped;

	dev_dstats_tx_add(dev, len);
	return NETDEV_TX_OK;

drop:
	kfree_skb(skb);
dropped:
	dev_dstats_tx_dropped(dev);
	/* report the dropped frame to the sender (e.g. TCP) like VLAN */
	return NET_XMIT_DROP;
}

/* PF_CAN receive callback for the frames matching the canxl_eth filter */
static void canxl_eth_rx(struct sk_buff *oskb, void *data)
{
	const struct canxl_frame *cxl = (struct canxl_frame *)oskb->data;
	struct net_device *dev = data;
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	unsigned int len;

	if (!can_is_canxl_skb(oskb))
		return;

	if (cxl->sdt != CAN_CIA_ETH_SDT && cxl->sdt != CAN_CIA_ETH_EXT_SDT)
		return;

	/* the SEC/RRS values have to match the canxl_eth settings */
	if ((cxl->flags & CANXL_ETH_FRAME_FLAGS) !=
	    (priv->cfg.flags & CANXL_ETH_FRAME_FLAGS))
		return;

	/* drop the echo of our own sent frames */
	if (oskb->sk == priv->sk)
		return;

	len = cxl->len;
	if (len < ETH_HLEN) {
		dev_dstats_rx_dropped(dev);
		return;
	}

	/* the skb is shared with all other PF_CAN receivers */
	skb = skb_clone(oskb, GFP_ATOMIC);
	if (!skb) {
		dev_dstats_rx_dropped(dev);
		return;
	}

	/* strip CAN specific content: the CAN XL data is the Ethernet frame */
	skb_ext_reset(skb);
	skb_pull(skb, CANXL_HDR_SIZE);

	/* CAN drivers set CHECKSUM_UNNECESSARY for CAN frames */
	skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = eth_type_trans(skb, dev);

	if (netif_rx(skb) == NET_RX_SUCCESS)
		dev_dstats_rx_add(dev, len);
	else
		dev_dstats_rx_dropped(dev);
}

static int canxl_eth_init(struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct sock *sk;

	/* unique PF_CAN struct sock to own the sent skbs */
	sk = sk_alloc(dev_net(priv->candev), PF_CAN, GFP_KERNEL,
		      &canxl_eth_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(NULL, sk);
	priv->sk = sk;

	netdev_hold(priv->candev, &priv->dev_tracker, GFP_KERNEL);
	return 0;
}

static void canxl_eth_uninit(struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	/* freed when the last skb referencing the struct sock is gone */
	sock_put(priv->sk);
	netdev_put(priv->candev, &priv->dev_tracker);
}

static int canxl_eth_open(struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);
	canid_t can_id, mask;
	int err;

	canxl_eth_rx_filter(&priv->cfg, &can_id, &mask);

#ifdef CANXL_ETH_HAVE_RX_REGISTER_INO
	err = can_rx_register(dev_net(priv->candev), priv->candev, can_id,
			      mask, canxl_eth_rx, dev, 0, priv->sk);
#else
	/* PF_CAN receiver identification string (no socket inode) */
	err = can_rx_register(dev_net(priv->candev), priv->candev, can_id,
			      mask, canxl_eth_rx, dev, DRV_NAME, priv->sk);
#endif
	if (err)
		return err;

	priv->rx_can_id = can_id;
	priv->rx_mask = mask;

	return 0;
}

static int canxl_eth_stop(struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	can_rx_unregister(dev_net(priv->candev), priv->candev, priv->rx_can_id,
			  priv->rx_mask, canxl_eth_rx, dev);
	return 0;
}

static int canxl_eth_get_iflink(const struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	return READ_ONCE(priv->candev->ifindex);
}

static const struct net_device_ops canxl_eth_netdev_ops = {
	.ndo_init		= canxl_eth_init,
	.ndo_uninit		= canxl_eth_uninit,
	.ndo_open		= canxl_eth_open,
	.ndo_stop		= canxl_eth_stop,
	.ndo_start_xmit		= canxl_eth_start_xmit,
	.ndo_get_iflink		= canxl_eth_get_iflink,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void canxl_eth_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &canxl_eth_netdev_ops;
	dev->needs_free_netdev = true;
	dev->lltx = true;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;

	/* the skb is modified in canxl_eth_start_xmit() */
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_NO_QUEUE;

	/* space for the CAN XL frame header in front of the Ethernet frame */
	dev->needed_headroom = CANXL_HDR_SIZE;

	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = CANXL_ETH_MAX_MTU;

	eth_hw_addr_random(dev);
}

static const struct nla_policy canxl_eth_policy[IFLA_CANXL_ETH_MAX + 1] = {
	[IFLA_CANXL_ETH_SDT] = NLA_POLICY_RANGE(NLA_U8, CAN_CIA_ETH_SDT,
						CAN_CIA_ETH_EXT_SDT),
	[IFLA_CANXL_ETH_TX_PRIO] = NLA_POLICY_MAX(NLA_U16, CANXL_PRIO_MASK),
	[IFLA_CANXL_ETH_RX_PRIO] = NLA_POLICY_MAX(NLA_U16, CANXL_PRIO_MASK),
	[IFLA_CANXL_ETH_RX_PRIO_MASK] = NLA_POLICY_MAX(NLA_U16, CANXL_PRIO_MASK),
	[IFLA_CANXL_ETH_TX_VCID] = { .type = NLA_U8 },
	[IFLA_CANXL_ETH_RX_VCID] = { .type = NLA_U8 },
	[IFLA_CANXL_ETH_RX_VCID_MASK] = { .type = NLA_U8 },
	[IFLA_CANXL_ETH_FLAGS] = NLA_POLICY_MASK(NLA_U8, CANXL_ETH_FLAGS_MASK),
};

static void canxl_eth_parse_cfg(struct canxl_eth_cfg *cfg,
				struct nlattr *data[])
{
	if (data[IFLA_CANXL_ETH_SDT])
		cfg->sdt = nla_get_u8(data[IFLA_CANXL_ETH_SDT]);

	if (data[IFLA_CANXL_ETH_TX_PRIO])
		cfg->tx_prio = nla_get_u16(data[IFLA_CANXL_ETH_TX_PRIO]);

	if (data[IFLA_CANXL_ETH_RX_PRIO])
		cfg->rx_prio = nla_get_u16(data[IFLA_CANXL_ETH_RX_PRIO]);

	if (data[IFLA_CANXL_ETH_RX_PRIO_MASK])
		cfg->rx_prio_mask = nla_get_u16(data[IFLA_CANXL_ETH_RX_PRIO_MASK]);

	if (data[IFLA_CANXL_ETH_TX_VCID])
		cfg->tx_vcid = nla_get_u8(data[IFLA_CANXL_ETH_TX_VCID]);

	if (data[IFLA_CANXL_ETH_RX_VCID])
		cfg->rx_vcid = nla_get_u8(data[IFLA_CANXL_ETH_RX_VCID]);

	if (data[IFLA_CANXL_ETH_RX_VCID_MASK])
		cfg->rx_vcid_mask = nla_get_u8(data[IFLA_CANXL_ETH_RX_VCID_MASK]);

	if (data[IFLA_CANXL_ETH_FLAGS])
		cfg->flags = nla_get_u8(data[IFLA_CANXL_ETH_FLAGS]);
}

static int canxl_eth_changelink(struct net_device *dev, struct nlattr *tb[],
				struct nlattr *data[],
				struct netlink_ext_ack *extack)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	if (!data)
		return 0;

	/* like a bound CAN_ISOTP socket the settings are fixed while up */
	if (netif_running(dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "settings can only be changed while down");
		return -EBUSY;
	}

	canxl_eth_parse_cfg(&priv->cfg, data);
	return 0;
}

#ifdef CANXL_ETH_HAVE_NEWLINK_PARAMS
static int canxl_eth_newlink(struct net_device *dev,
			     struct rtnl_newlink_params *params,
			     struct netlink_ext_ack *extack)
{
	struct net *link_net = rtnl_newlink_link_net(params);
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct nlattr **data = params->data;
	struct nlattr **tb = params->tb;
	struct net_device *candev;
#else
/* newlink() without struct rtnl_newlink_params */
static int canxl_eth_newlink(struct net *src_net, struct net_device *dev,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct net *link_net = src_net;
	struct canxl_eth_priv *priv = netdev_priv(dev);
	struct net_device *candev;
#endif
	int err;

	if (!tb[IFLA_LINK]) {
		NL_SET_ERR_MSG_MOD(extack, "CAN XL link not specified");
		return -EINVAL;
	}

	candev = __dev_get_by_index(link_net, nla_get_u32(tb[IFLA_LINK]));
	if (!candev) {
		NL_SET_ERR_MSG_MOD(extack, "CAN XL link does not exist");
		return -ENODEV;
	}

	if (!can_get_ml_priv(candev)) {
		NL_SET_ERR_MSG_MOD(extack, "link is not a CAN interface");
		return -EINVAL;
	}

	/* the CAN XL settings have to be provided (VCID and flags optional) */
	if (!data || !data[IFLA_CANXL_ETH_SDT] ||
	    !data[IFLA_CANXL_ETH_TX_PRIO] || !data[IFLA_CANXL_ETH_RX_PRIO] ||
	    !data[IFLA_CANXL_ETH_RX_PRIO_MASK]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "sdt, tx_prio, rx_prio and rx_prio_mask required");
		return -EINVAL;
	}

	priv->dev = dev;
	priv->candev = candev;

	/* defaults: no VCID, echo on, SEC = 0, RRS = 0 */
	priv->cfg = (struct canxl_eth_cfg) {
		.flags = CANXL_ETH_FLAG_ECHO,
	};
	canxl_eth_parse_cfg(&priv->cfg, data);

	/* start without carrier to get clean operational state changes */
	netif_carrier_off(dev);

	err = register_netdevice(dev);
	if (err)
		return err;

	err = netdev_upper_dev_link(candev, dev, extack);
	if (err) {
		unregister_netdevice(dev);
		return err;
	}

	list_add(&priv->list, &canxl_eth_list);
	canxl_eth_update_carrier(dev);

	return 0;
}

static void canxl_eth_dellink(struct net_device *dev, struct list_head *head)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	list_del(&priv->list);
	netdev_upper_dev_unlink(priv->candev, dev);
	unregister_netdevice_queue(dev, head);
}

static size_t canxl_eth_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(u8)) +	/* IFLA_CANXL_ETH_SDT */
	       nla_total_size(sizeof(u16)) +	/* IFLA_CANXL_ETH_TX_PRIO */
	       nla_total_size(sizeof(u16)) +	/* IFLA_CANXL_ETH_RX_PRIO */
	       nla_total_size(sizeof(u16)) +	/* IFLA_CANXL_ETH_RX_PRIO_MASK */
	       nla_total_size(sizeof(u8)) +	/* IFLA_CANXL_ETH_TX_VCID */
	       nla_total_size(sizeof(u8)) +	/* IFLA_CANXL_ETH_RX_VCID */
	       nla_total_size(sizeof(u8)) +	/* IFLA_CANXL_ETH_RX_VCID_MASK */
	       nla_total_size(sizeof(u8));	/* IFLA_CANXL_ETH_FLAGS */
}

static int canxl_eth_fill_info(struct sk_buff *skb,
			       const struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);
	const struct canxl_eth_cfg *cfg = &priv->cfg;

	if (nla_put_u8(skb, IFLA_CANXL_ETH_SDT, cfg->sdt) ||
	    nla_put_u16(skb, IFLA_CANXL_ETH_TX_PRIO, cfg->tx_prio) ||
	    nla_put_u16(skb, IFLA_CANXL_ETH_RX_PRIO, cfg->rx_prio) ||
	    nla_put_u16(skb, IFLA_CANXL_ETH_RX_PRIO_MASK, cfg->rx_prio_mask) ||
	    nla_put_u8(skb, IFLA_CANXL_ETH_TX_VCID, cfg->tx_vcid) ||
	    nla_put_u8(skb, IFLA_CANXL_ETH_RX_VCID, cfg->rx_vcid) ||
	    nla_put_u8(skb, IFLA_CANXL_ETH_RX_VCID_MASK, cfg->rx_vcid_mask) ||
	    nla_put_u8(skb, IFLA_CANXL_ETH_FLAGS, cfg->flags))
		return -EMSGSIZE;

	return 0;
}

static struct net *canxl_eth_get_link_net(const struct net_device *dev)
{
	struct canxl_eth_priv *priv = netdev_priv(dev);

	return dev_net(priv->candev);
}

static struct rtnl_link_ops canxl_eth_link_ops = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct canxl_eth_priv),
	.setup		= canxl_eth_setup,
	.maxtype	= IFLA_CANXL_ETH_MAX,
	.policy		= canxl_eth_policy,
	.newlink	= canxl_eth_newlink,
	.changelink	= canxl_eth_changelink,
	.dellink	= canxl_eth_dellink,
	.get_size	= canxl_eth_get_size,
	.fill_info	= canxl_eth_fill_info,
	.get_link_net	= canxl_eth_get_link_net,
};

/* track the state of the CAN XL interfaces used by canxl_eth devices */
static int canxl_eth_notifier(struct notifier_block *nb, unsigned long msg,
			      void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct canxl_eth_priv *priv, *tmp;
	LIST_HEAD(list_kill);

	if (!can_get_ml_priv(dev))
		return NOTIFY_DONE;

	switch (msg) {
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
	case NETDEV_CHANGEMTU:
		list_for_each_entry(priv, &canxl_eth_list, list) {
			if (priv->candev == dev)
				canxl_eth_update_carrier(priv->dev);
		}
		break;

	case NETDEV_UNREGISTER:
		/* the CAN XL interface vanishes (or changes its netns) */
		list_for_each_entry_safe(priv, tmp, &canxl_eth_list, list) {
			if (priv->candev == dev)
				canxl_eth_dellink(priv->dev, &list_kill);
		}
		unregister_netdevice_many(&list_kill);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block canxl_eth_notifier_block __read_mostly = {
	.notifier_call = canxl_eth_notifier,
};

static __init int canxl_eth_module_init(void)
{
	int err;

	pr_info("can: canxl_eth Ethernet over CAN XL (CiA 611-1 SDT 04h/05h)\n");

	err = proto_register(&canxl_eth_proto, 0);
	if (err)
		return err;

	err = register_netdevice_notifier(&canxl_eth_notifier_block);
	if (err)
		goto out_proto_unregister;

	err = rtnl_link_register(&canxl_eth_link_ops);
	if (err)
		goto out_unregister_notifier;

	return 0;

out_unregister_notifier:
	unregister_netdevice_notifier(&canxl_eth_notifier_block);
out_proto_unregister:
	proto_unregister(&canxl_eth_proto);
	return err;
}

static __exit void canxl_eth_module_exit(void)
{
	rtnl_link_unregister(&canxl_eth_link_ops);
	unregister_netdevice_notifier(&canxl_eth_notifier_block);
	proto_unregister(&canxl_eth_proto);
}

module_init(canxl_eth_module_init);
module_exit(canxl_eth_module_exit);
