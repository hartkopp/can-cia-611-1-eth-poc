# Concept of the canxl_eth netdevice

CiA 611-1 specifies the service data unit types (SDT) 0x04 and 0x05 to transport
IEEE 802.3 MAC frames in CAN XL frames. The `canxl_eth` netdevice implements
this mapping in the Linux network layer as an Ethernet interface which is
stacked on top of a CAN XL interface.

The code snippets in this document are taken from `net/can/xl_eth.c` and
`include/uapi/linux/can/xl_eth.h` of the Linux patch.

## Design goals

* **Hardware independent:** `canxl_eth` does not touch the CAN XL driver. It is
  a regular user of the PF_CAN network layer (`can_send()` and
  `can_rx_register()`), like the CAN_RAW, CAN_BCM or CAN_ISOTP protocols.
  Therefore it works with every CAN XL driver and with the virtual CAN
  drivers `vcan` and `vxcan`.
* **Parallel operation:** The CAN XL interface remains fully usable for other
  CAN CC/FD/XL traffic (e.g. other SDTs) while it transports Ethernet frames.
* **Integration:** The Ethernet traffic is visible to other PF_CAN users, e.g.
  for log files with `candump`, and CAN XL frames with SDT 0x04/0x05 from local
  PF_CAN applications are received by the `canxl_eth` device.
* **Zero-copy:** The Ethernet frame and the CAN XL frame share the same
  skb data. Only the CAN XL frame header is pushed or pulled.

## Layer model

```
        Ethernet stack (IPv4, IPv6, ARP, VLAN, bridge, ...)
                       |  ndo_start_xmit()      ^  netif_rx()
                       v                        |
              +-------------------------------------------+
              |   xleth0 (canxl_eth, rtnl link type)      |
              |   - CAN XL header push/pull               |
              |   - AF / VCID mapping (SDT 0x04/0x05)     |
              |   - own struct sock as skb owner          |
              +-------------------------------------------+
                       |  can_send()            ^  can_rx_register() callback
                       v                        |
              +-------------------------------------------+
              |   PF_CAN network layer (af_can)           |<--> CAN_RAW, CAN_BCM, ...
              +-------------------------------------------+
                       |  dev_queue_xmit()      ^  netif_rx()
                       v                        |
              +-------------------------------------------+
              |   can0 (CAN XL netdevice / driver)        |
              +-------------------------------------------+
```

The `canxl_eth` device is linked to its CAN XL interface as upper device
(`netdev_upper_dev_link()`) and reports the CAN XL interface as its link
(`ndo_get_iflink()`), so that it shows up as `xleth0@can0`.

## Frame mapping

The Ethernet frame without FCS is the data field of the CAN XL frame. As
`struct canxl_frame` has a 12 byte header in front of the data, a CAN XL skb
and an Ethernet skb only differ in this header:

```
struct canxl_frame:  | prio | flags | sdt | len | af |  data[] (Ethernet frame)  |
                     |<---- CANXL_HDR_SIZE (12) ---->|<-- DA | SA | type | ... -->|
```

| CAN XL field | SDT 0x04 | SDT 0x05 |
|---|---|---|
| priority ID | `tx_prio` | `tx_prio` |
| VCID | `tx_vcid` | lower 8 bit of the VID of a C-VLAN/S-VLAN tag (or 0x00) |
| AF | `CANXL_ETH_SDT04_AF_PAD` (0xCCCCCCCC) | 1st, 4th, 5th and 6th octet of the destination MAC address |
| SEC / RRS | `CANXL_ETH_FLAG_SEC` / `CANXL_ETH_FLAG_RRS` | `CANXL_ETH_FLAG_SEC` / `CANXL_ETH_FLAG_RRS` |
| data | Ethernet frame (without FCS) | Ethernet frame (without FCS) |

```c
/* CiA 611-1 SDT 05h: AF = DA octets 1, 4, 5, 6 (MSB first) */
static u32 canxl_eth_af(const struct ethhdr *eth)
{
	const u8 *da = eth->h_dest;

	return (u32)da[0] << 24 | (u32)da[3] << 16 | (u32)da[4] << 8 | da[5];
}
```

## Configuration (netlink)

The `canxl_eth` device is an rtnl link type with its own netlink attributes
(`include/uapi/linux/can/xl_eth.h`):

```c
enum {
	IFLA_CANXL_ETH_UNSPEC,
	IFLA_CANXL_ETH_SDT,		/* u8: CAN_CIA_ETH_SDT or _ETH_EXT_SDT */
	IFLA_CANXL_ETH_TX_PRIO,		/* u16: 11 bit priority ID (tx) */
	IFLA_CANXL_ETH_RX_PRIO,		/* u16: 11 bit priority ID (rx filter) */
	IFLA_CANXL_ETH_RX_PRIO_MASK,	/* u16: 11 bit priority ID mask (rx) */
	IFLA_CANXL_ETH_TX_VCID,		/* u8: VCID (tx, only for SDT 04h) */
	IFLA_CANXL_ETH_RX_VCID,		/* u8: VCID (rx filter) */
	IFLA_CANXL_ETH_RX_VCID_MASK,	/* u8: VCID mask (rx filter) */
	IFLA_CANXL_ETH_FLAGS,		/* u8: CANXL_ETH_FLAG_* */
	...
};
```

The flags reuse the CAN XL frame flag values. The value 0x80 is `CANXL_XLF` in
the CAN XL frame which is always set, so it is free to enable the local echo:

```c
#define CANXL_ETH_FLAG_SEC	CANXL_SEC /* set/check SEC in CAN XL frames */
#define CANXL_ETH_FLAG_RRS	CANXL_RRS /* set/check RRS in CAN XL frames */
#define CANXL_ETH_FLAG_ECHO	0x80	  /* local echo of sent frames */
```

Like the addressing of a CAN_ISOTP socket at `bind()` time, the CAN XL settings
`sdt`, `tx_prio`, `rx_prio` and `rx_prio_mask` are mandatory when the device is
created. Only the VCID settings (default: no VCID) and the flags (default:
`CANXL_ETH_FLAG_ECHO`) are optional. The settings are fixed while the device is
up:

```c
	/* like a bound CAN_ISOTP socket the settings are fixed while up */
	if (netif_running(dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "settings can only be changed while down");
		return -EBUSY;
	}
```

## Transmit path

`canxl_eth_start_xmit()` turns the Ethernet skb into a CAN XL skb by pushing
the CAN XL frame header in front of the Ethernet frame (`needed_headroom` of
the device is `CANXL_HDR_SIZE`):

```c
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
```

Then the skb is handed over to the PF_CAN network layer with or without local
echo:

```c
	/* the skb has to be owned by a PF_CAN socket (see top of file) */
	skb_orphan(skb);
	can_skb_set_owner(skb, priv->sk);
	skb->dev = candev;

	/* can_send() consumes the skb in any case */
	if (can_send(skb, !!(priv->cfg.flags & CANXL_ETH_FLAG_ECHO)))
		dev_dstats_tx_dropped(dev);
	else
		dev_dstats_tx_add(dev, len);
```

## skb ownership: the canxl_eth struct sock

An skb from the Ethernet stack is owned by its originating socket (e.g. a TCP
socket) or by no socket at all. The PF_CAN protocols expect `skb->sk` to be a
PF_CAN socket:

* the echo skb handling takes a reference on `skb->sk` (`can_skb_set_owner()`)
* CAN_RAW marks frames with `skb->sk != NULL` as locally generated
  (`MSG_DONTROUTE`, e.g. shown as TX frames by `candump -x`)
* CAN_BCM distinguishes local and own traffic (`RX_LOCAL`, `RX_OWN`)

Therefore each `canxl_eth` device owns a socket-less PF_CAN `struct sock` which
takes over the ownership of the skbs before `can_send()`:

```c
/* protocol for the socket-less struct sock of the canxl_eth devices */
static struct proto canxl_eth_proto = {
	.name		= "CANXL_ETH",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct sock),
};

static int canxl_eth_init(struct net_device *dev)
{
	...
	/* unique PF_CAN struct sock to own the sent skbs */
	sk = sk_alloc(dev_net(priv->candev), PF_CAN, GFP_KERNEL,
		      &canxl_eth_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(NULL, sk);
	priv->sk = sk;
	...
}
```

The `struct sock` is freed when the last skb referencing it is gone (e.g. an
echo skb in a CAN driver). As it holds a reference to the module, the
`canxl_eth` module can only be unloaded after all `canxl_eth` devices have been
removed.

## Receive path

The receive filter is registered at the PF_CAN network layer when the device
is set up (`ndo_open()`). The priority ID and VCID filter settings are combined
into a single PF_CAN filter, as the VCID is located in the bits 16..23 of the
CAN XL `prio` element which is matched as `can_id`:

```c
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
```

Without a VCID filter `CAN_EFF_FLAG` and `CAN_RTR_FLAG` are added to the mask.
For a single priority ID (`rx_prio_mask 0x7FF`, e.g. point-to-point) the PF_CAN
core then uses its single SFF identifier filter list (fast path), which only
checks the lower 11 bits of the CAN XL `prio` element and therefore ignores
the VCID.

The receive callback checks the CAN XL frame, drops the echo of the own sent
frames (identified by the `struct sock`) and turns the CAN XL skb into an
Ethernet skb by pulling the CAN XL frame header:

```c
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
	...
	/* the skb is shared with all other PF_CAN receivers */
	skb = skb_clone(oskb, GFP_ATOMIC);
	...
	/* strip CAN specific content: the CAN XL data is the Ethernet frame */
	skb_ext_reset(skb);
	skb_pull(skb, CANXL_HDR_SIZE);

	/* CAN drivers set CHECKSUM_UNNECESSARY for CAN frames */
	skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = eth_type_trans(skb, dev);

	if (netif_rx(skb) == NET_RX_SUCCESS)
		dev_dstats_rx_add(dev, len);
```

Received CAN XL frames with SDT 0x04 and 0x05 are accepted independently of
the configured `sdt` for the sent frames.

## Device lifecycle

* **Carrier:** The carrier of the `canxl_eth` device follows the CAN XL
  interface. A netdevice notifier updates the carrier on `NETDEV_UP`,
  `NETDEV_DOWN`, `NETDEV_CHANGE` and `NETDEV_CHANGEMTU`:

  ```c
	if (netif_running(candev) && netif_carrier_ok(candev) &&
	    can_cap_enabled(candev, CAN_CAP_XL))
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
  ```

* **Read-only mode:** No frames are sent when the CAN interface is in a
  read-only mode (listen-only or restricted operation, `CAN_CAP_RO`).
* **Removal:** On `NETDEV_UNREGISTER` of the CAN XL interface all `canxl_eth`
  devices on top of it are removed.

## Known limitations

* The sent skbs are orphaned from their original socket. Therefore e.g. TCP
  has no transmit backpressure from the `canxl_eth` device. When the transmit
  queue of the CAN XL interface is full the frames are dropped and TCP
  retransmits them.
* The IP header of received frames is not 4 byte aligned (offset 26 from the
  start of the CAN XL frame). This is supported but may cost some performance
  on some architectures.

---
_Documented by Claude Opus 5.5_
