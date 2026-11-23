/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * compat.h - compatibility helpers for the out-of-tree build of canxl_eth
 *
 * The canxl_eth source (xl_eth.c) is written for the current Linux kernel.
 * This file provides the missing helpers for the supported older kernels
 * (Linux 6.12 and later). Code which differs inside of functions is
 * handled with sections in xl_eth.c.
 *
 * The availability of the kernel API features is detected in the Kbuild
 * file by checking the kernel headers (CANXL_ETH_HAVE_* defines). A check
 * of LINUX_VERSION_CODE is not sufficient as stable kernels (e.g. the
 * Debian kernels) may contain backports of newer kernel API features.
 *
 * Copyright (c) 2026 Oliver Hartkopp <socketcan@hartkopp.net>
 */

#ifndef CANXL_ETH_COMPAT_H
#define CANXL_ETH_COMPAT_H

#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/u64_stats_sync.h>
#include <linux/can/dev.h>
#include <linux/can/can-ml.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
#error "canxl_eth needs at least Linux 6.12"
#endif

/* CANXL_RRS in the uapi linux/can.h (Linux 6.15) */
#ifndef CANXL_RRS
#define CANXL_RRS 0x02 /* Remote Request Substitution */
#endif

/* dev_dstats_* helpers (Linux 6.14) */
#ifndef CANXL_ETH_HAVE_DSTATS_HELPERS
static inline void dev_dstats_rx_add(struct net_device *dev,
				     unsigned int len)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	u64_stats_inc(&dstats->rx_packets);
	u64_stats_add(&dstats->rx_bytes, len);
	u64_stats_update_end(&dstats->syncp);
}

static inline void dev_dstats_rx_dropped(struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	u64_stats_inc(&dstats->rx_drops);
	u64_stats_update_end(&dstats->syncp);
}

static inline void dev_dstats_tx_add(struct net_device *dev,
				     unsigned int len)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	u64_stats_inc(&dstats->tx_packets);
	u64_stats_add(&dstats->tx_bytes, len);
	u64_stats_update_end(&dstats->syncp);
}

static inline void dev_dstats_tx_dropped(struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	u64_stats_inc(&dstats->tx_drops);
	u64_stats_update_end(&dstats->syncp);
}
#endif

/*
 * CAN device capabilities (Linux 6.19)
 * Without CAN device capabilities the CAN XL capability is derived from the
 * CAN XL MTU of the CAN interface. A read-only mode can not be detected.
 */
#ifndef CANXL_ETH_HAVE_CAN_CAP
#define CAN_CAP_XL BIT(2) /* CAN XL */
#define CAN_CAP_RO BIT(3) /* read-only mode (LISTEN/RESTRICTED) */

static inline bool can_cap_enabled(struct net_device *dev, u32 cap)
{
	if (cap == CAN_CAP_XL)
		return can_is_canxl_dev_mtu(READ_ONCE(dev->mtu));

	return false;
}
#endif

#endif /* CANXL_ETH_COMPAT_H */
