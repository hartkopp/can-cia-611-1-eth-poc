/* SPDX-License-Identifier: ((GPL-2.0-only WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * linux/can/xl_eth.h
 *
 * Definitions for the CAN XL Ethernet netdevice (canxl_eth) which tunnels
 * IEEE 802.3 MAC frames in CAN XL frames as specified in CiA 611-1
 * (CAN XL higher-layer functions - Part 1: Service data unit types).
 */

#ifndef _UAPI_CAN_XL_ETH_H
#define _UAPI_CAN_XL_ETH_H

#include <linux/can.h>

/* CAN CiA 611-1 Service Data Unit Types for IEEE 802.3 MAC frames */
#define CAN_CIA_ETH_SDT		0x04	/* IEEE 802.3 (MAC frame) */
#define CAN_CIA_ETH_EXT_SDT	0x05	/* IEEE 802.3 (MAC frame) extended */

/* content of the CAN XL acceptance field (AF) for SDT 04h */
#define CANXL_ETH_SDT04_AF_PAD	0xCCCCCCCC /* prevent bit-stuffing */

/* flags for IFLA_CANXL_ETH_FLAGS (u8) */
#define CANXL_ETH_FLAG_SEC	CANXL_SEC /* set/check SEC in CAN XL frames */
#define CANXL_ETH_FLAG_RRS	CANXL_RRS /* set/check RRS in CAN XL frames */
#define CANXL_ETH_FLAG_ECHO	0x80	  /* local echo of sent frames */

/*
 * canxl_eth specific netlink attributes (IFLA_INFO_DATA)
 *
 * Like the addressing of a CAN_ISOTP socket at bind() time the attributes
 * SDT, TX_PRIO, RX_PRIO and RX_PRIO_MASK are mandatory when creating the
 * canxl_eth device. The VCID attributes (default 0 => no VCID) and FLAGS
 * (default CANXL_ETH_FLAG_ECHO) are optional. All values can only be
 * changed while the canxl_eth device is down. FLAGS always replaces all
 * flags.
 *
 * The CAN XL frames are created with CANXL_XLF and the SEC/RRS values from
 * CANXL_ETH_FLAG_SEC/CANXL_ETH_FLAG_RRS. The value 0x80 (CANXL_XLF in the
 * CAN XL frame) is used for CANXL_ETH_FLAG_ECHO as CANXL_XLF is always set.
 *
 * TX path:
 *   SDT 04h: AF = CANXL_ETH_SDT04_AF_PAD, VCID = IFLA_CANXL_ETH_TX_VCID
 *   SDT 05h: AF = 1st/4th/5th/6th octet of the destination MAC address,
 *            VCID = lower 8 bit of the VID of the first VLAN tag (or 0)
 *
 * RX path:
 *   CAN XL frames with SDT 04h and SDT 05h are accepted when
 *   (prio & rx_prio_mask) == (rx_prio & rx_prio_mask) and
 *   (vcid & rx_vcid_mask) == (rx_vcid & rx_vcid_mask).
 *   With rx_vcid_mask = 0 (default) all VCIDs are accepted.
 *   The SEC/RRS values of the CAN XL frame have to match the
 *   CANXL_ETH_FLAG_SEC/CANXL_ETH_FLAG_RRS settings.
 */
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

	__IFLA_CANXL_ETH_MAX
#define IFLA_CANXL_ETH_MAX	(__IFLA_CANXL_ETH_MAX - 1)
};

#endif /* !_UAPI_CAN_XL_ETH_H */
