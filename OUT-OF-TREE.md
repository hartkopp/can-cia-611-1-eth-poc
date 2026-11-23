# Out-of-tree build of canxl_eth

The `out-of-tree` directory contains the `canxl_eth` module for existing
Linux kernels, e.g. the kernel of a Debian installation. The kernel does not
need to be patched or rebuilt: only the `canxl_eth` module is built against
the kernel headers of the running kernel.

For the configuration of `canxl_eth` devices the patched `ip` tool from
iproute2 is needed in any case (see [INSTALL.md](INSTALL.md)). It does not
depend on the kernel version.

## Supported kernels

* **Linux 6.12 and later**
* Debian 13 "trixie" (Linux 6.12) and newer Debian kernels (e.g. from
  backports)
* Debian 12 "bookworm" (Linux 6.1) does not support CAN XL. Use a Linux 6.12
  (or later) kernel from bookworm-backports.

The kernel has to provide the CAN subsystem (`CONFIG_CAN`, `CONFIG_CAN_DEV`)
and a CAN XL capable driver (or `vcan`/`vxcan` for tests). The Debian kernels
provide the CAN subsystem as modules.

## Files

* `out-of-tree/xl_eth.c` : `canxl_eth` source with compatibility sections
* `out-of-tree/compat.h` : helpers which are missing in older kernels
* `out-of-tree/include/uapi/linux/can/xl_eth.h` : netlink attributes and flags
* `out-of-tree/Kbuild` : Kbuild file for the module (with detection of kernel API features)
* `out-of-tree/Makefile` : build, install and DKMS targets
* `out-of-tree/dkms.conf` : DKMS configuration

The Linux patch (`linux-7.4-canxl_eth.patch`) is the reference. The file
`out-of-tree/xl_eth.c` is the source from the Linux patch with additional
compatibility sections for older kernels. With Linux 7.4 and later the
out-of-tree module uses exactly the code of the Linux patch.

## Requirements (Debian)

```
sudo apt install build-essential dkms linux-headers-$(uname -r)
```

## Build and test

```
cd out-of-tree
make
```

The module can be loaded directly from the build directory for a first test.
`insmod` does not load the CAN core module automatically:

```
sudo modprobe can
sudo insmod canxl_eth.ko
```

Then create and configure `canxl_eth` devices with the patched `ip` tool (see
[SETUP.md](SETUP.md)). The module can be unloaded after all `canxl_eth`
devices have been removed:

```
sudo rmmod canxl_eth
```

To build the module for another installed kernel set `KDIR` to its build
directory, e.g. `make KDIR=/lib/modules/<version>/build`.

## Install

Use either DKMS or `make install`, not both. When switching from
`make install` to DKMS, remove the installed module with `sudo make uninstall`
first. Otherwise DKMS archives the module installed with `make install` and
restores it when the DKMS installation is removed.

### With DKMS (recommended)

DKMS rebuilds the module automatically when a new kernel (and its headers) is
installed. DKMS is not installed by default and has to be installed first
(`sudo apt install dkms`, see Requirements):

```
sudo make dkms-install
dkms status
```

To update the module (e.g. after `git pull`) run `sudo make dkms-install`
again: an existing DKMS installation of the same version is replaced. The
updated module is used after reloading it. This requires that all `canxl_eth`
devices have been removed:

```
sudo rmmod canxl_eth
sudo modprobe canxl_eth
```

Remove the DKMS installation:

```
sudo make dkms-uninstall
```

### Without DKMS

```
sudo make install
```

This installs the module for the running kernel to
`/lib/modules/$(uname -r)/updates` and updates the module dependencies with
`depmod`. The module has to be built and installed again for every new kernel.

With the Debian `linux-headers` packages the installation shows these
messages, which are expected:

* `SSL error ... sign-file: ./output/signing_key.pem` : the headers do not
  contain the signing key of the Debian kernel. The module is installed
  unsigned (see Secure Boot).
* `Warning: modules_install: missing 'System.map' file. Skipping depmod.` :
  `depmod` is called by `make install` afterwards.

Remove the installed module:

```
sudo make uninstall
```

After the installation (with or without DKMS) the module is loaded
automatically when a `canxl_eth` device is created with
`ip link add ... type canxl_eth`.

## Secure Boot

With enabled Secure Boot (`mokutil --sb-state`) the Debian kernels only load
signed modules. The module built with `make` (and installed with
`make install`) is not signed and can not be loaded in this case.

DKMS signs the modules with its own Machine Owner Key (MOK). This key has to
be enrolled once:

```
sudo mokutil --import /var/lib/dkms/mok.pub
```

After a reboot the key enrollment has to be confirmed in the MOK manager.
Afterwards the modules installed with DKMS can be loaded.

## Kernel taint

As an out-of-tree module `canxl_eth` taints the kernel with the flag `O`
(`cat /proc/sys/kernel/tainted`, `dmesg`). This is expected and has no
functional impact.

## Compatibility

The differences between the supported kernels are handled in
`out-of-tree/xl_eth.c` and `out-of-tree/compat.h`.

The kernel API features are detected at build time by checking the headers
of the target kernel (`out-of-tree/Kbuild`, `CANXL_ETH_HAVE_*` defines). A
check of the kernel version (`LINUX_VERSION_CODE`) is not sufficient, as
stable kernels may contain backports of newer kernel API features. E.g. the
Debian kernel 6.12.117 already provides the `dev_dstats_*()` helpers of
Linux 6.14.

| Mainline | Kernel API feature | Define | Without the feature |
|---|---|---|---|
| 6.14 | `dev_dstats_*()` helpers | `CANXL_ETH_HAVE_DSTATS_HELPERS` | helpers in `compat.h` |
| 6.15 | `newlink()` with `struct rtnl_newlink_params` | `CANXL_ETH_HAVE_NEWLINK_PARAMS` | old `newlink()` signature |
| 6.15 | `CANXL_RRS` in the uapi `linux/can.h` | (`#ifndef CANXL_RRS`) | `CANXL_RRS` in `compat.h` |
| 6.19 | CAN device capabilities (`can_cap_enabled()`) | `CANXL_ETH_HAVE_CAN_CAP` | CAN XL is derived from the CAN XL MTU of the CAN interface, no read-only mode detection |
| 7.0 | CAN skb extension `SKB_EXT_CAN` | `CANXL_ETH_HAVE_CAN_SKB_EXT` | `struct can_skb_priv` at `skb->head`: additional headroom in the transmit path |
| 7.4 | `can_rx_register()` with the socket inode | `CANXL_ETH_HAVE_RX_REGISTER_INO` | identification string `canxl_eth` |

The detected features are shown in the compiler command line with
`make V=1`.

Limitations of older kernels:

* Without `CANXL_RRS` in the kernel headers the CAN XL RRS flag is not defined
  by the kernel. Whether `rrs on` sets the RRS bit on the CAN bus depends on
  the CAN XL driver.
* Without the CAN device capabilities the `canxl_eth` device can not detect a
  CAN interface in listen-only or restricted operation mode. The Ethernet
  frames are handed to the CAN interface anyway.

---
_Documented by Claude Opus 5.5_
