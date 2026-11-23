# Installation

The `canxl_eth` support consists of two patches:

* `linux-7.4-canxl_eth.patch` : the `canxl_eth` netdevice (Linux kernel module `canxl_eth`)
* `iproute2-7.4-canxl_eth.patch` : the `canxl_eth` support for the `ip` tool

The examples assume that this repository and the Linux/iproute2 sources are
located in the same directory:

```
work/
├── can-cia-611-1-eth-poc/
├── linux/
└── iproute2/
```

## Linux kernel

To use `canxl_eth` with an existing kernel (Linux 6.12 and later, e.g.
Debian 13) without patching and building the kernel, see
[OUT-OF-TREE.md](OUT-OF-TREE.md). The patched iproute2 is needed in any case.

### Get the sources and apply the patch

The patch has been created for Linux 7.4 and applies to Linux 7.4-rc4.

The `canxl_eth` netdevice builds on comparatively stable kernel APIs (the
PF_CAN network layer, rtnl link types and netdevices). Therefore it is best
to try the latest Linux kernel first, e.g. the latest release candidate or
release from https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
(replace `v7.4-rc4` with the latest tag):

```
git clone --depth 1 --branch v7.4-rc4 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
cd linux
git am ../can-cia-611-1-eth-poc/linux-7.4-canxl_eth.patch
```

`git am` needs a configured git user (`git config user.name` and
`git config user.email`). Alternatively the patch can be applied with
`patch -p1 < ../can-cia-611-1-eth-poc/linux-7.4-canxl_eth.patch`.

The patch adds these files:

* `net/can/xl_eth.c` : the `canxl_eth` netdevice
* `include/uapi/linux/can/xl_eth.h` : netlink attributes and flags
* `net/can/Kconfig`, `net/can/Makefile` : `CONFIG_CAN_XL_ETH`

### Configure the kernel

Start with the configuration of the running kernel (or any other
configuration) and enable `CONFIG_CAN_XL_ETH` as a module:

```
cp /boot/config-$(uname -r) .config
make olddefconfig
scripts/config --module CAN_XL_ETH
make olddefconfig
grep CAN_XL_ETH .config
```

The output should be `CONFIG_CAN_XL_ETH=m`. In `make menuconfig` the option
can be found at:

```
Networking support
  -> CAN bus subsystem support
    -> Ethernet over CAN XL (CiA 611-1 SDT 04h/05h)
```

Additionally the CAN subsystem (`CONFIG_CAN`, `CONFIG_CAN_DEV`) and a
CAN XL capable driver are required. For tests without CAN XL hardware the
virtual CAN drivers `CONFIG_CAN_VCAN` and `CONFIG_CAN_VXCAN` support CAN XL.

### Build and install

```
make -j$(nproc)
sudo make modules_install
sudo make install
```

Reboot into the new kernel and check the module:

```
modinfo canxl_eth
```

The module is loaded automatically when a `canxl_eth` device is created
with `ip link add ... type canxl_eth` (or manually with
`sudo modprobe canxl_eth`). The module can only be unloaded after all
`canxl_eth` devices have been removed.

## iproute2

### Get the sources and apply the patch

The patch is based on the iproute2 main branch for the upcoming iproute2 7.4
(after iproute2 7.3.0).

```
git clone https://git.kernel.org/pub/scm/network/iproute2/iproute2.git
cd iproute2
git am ../can-cia-611-1-eth-poc/iproute2-7.4-canxl_eth.patch
```

The patch adds these files:

* `ip/iplink_canxl_eth.c` : `canxl_eth` support for `ip link`
* `include/uapi/linux/can/xl_eth.h` : copy of the Linux uapi header
* `ip/Makefile`, `man/man8/ip-link.8.in` : build and documentation

### Build

The `libmnl` library is recommended: Without `libmnl` the `ip` tool does not
show the error messages of the kernel (e.g.
`sdt, tx_prio, rx_prio and rx_prio_mask required` or
`settings can only be changed while down`) but only the error code (e.g.
`RTNETLINK answers: Invalid argument`). On Debian:

```
sudo apt install libmnl-dev pkg-config
```

```
./configure
make
```

`./configure` should report `libmnl support: yes`. The other optional
libraries reported by `./configure` are not needed for `canxl_eth`.

### Use `ip` without installation

The `ip` tool from the distribution does not know the `canxl_eth` options
and fails with

```
Garbage instead of arguments "sdt ...". Try "ip link help".
```

Instead of installing iproute2 the build artifact `ip/ip` can be called
directly from the iproute2 source directory:

```
./ip/ip link help canxl_eth
sudo ./ip/ip link add link can0 name xleth0 type canxl_eth sdt 5 tx_prio 0x201 rx_prio 0x200 rx_prio_mask 0x700
sudo ./ip/ip -d link show xleth0
```

The updated manual page can be read without installation too:

```
man ./man/man8/ip-link.8
```

All other `ip` commands (e.g. `ip addr`, `ip route`) can be used with the
distribution's `ip` tool as usual. Only the creation and the configuration
of `canxl_eth` devices need the patched `ip`.

### Install (optional)

```
sudo make install
```

**Note:** This installs `ip` to `/sbin` and replaces the `ip` tool of the
distribution.

---
_Documented by Claude Opus 5.5_
