# can-cia-611-1-eth-poc
PoC for CAN CiA 611-1 Ethernet over CAN XL (SDT 0x04 / 0x05)

* Linux netdevice `canxl_eth`: an Ethernet interface stacked on top of a CAN XL interface
* IEEE 802.3 MAC frames are tunneled in CAN XL frames with the SDT 0x04 (IEEE 802.3 MAC frame) or SDT 0x05 (IEEE 802.3 MAC frame extended)
* Ethernet and other CAN XL traffic (other SDTs) share the same CAN XL interface in parallel
* hardware independent: `canxl_eth` is a regular user of the Linux PF_CAN network layer
* the Ethernet traffic is visible for PF_CAN tools like `candump` (e.g. for log files)
* configuration with iproute2 (`ip link ... type canxl_eth`)
* for discussion about the SDTs 0x04 and 0x05

```
   IPv4 / IPv6 / ARP / VLAN ...           candump / cansend / ...
              |                                    |
     +------------------+                          |
     |  xleth0          |  canxl_eth netdevice     |
     +------------------+                          |
              |   can_send() / can_rx_register()   |
     +-----------------------------------------------------+
     |                PF_CAN network layer                 |
     +-----------------------------------------------------+
                               |
                      +------------------+
                      |  can0            |  CAN XL netdevice
                      +------------------+  (any CAN XL driver)
```

### Files

* linux-7.4-canxl_eth.patch : Linux kernel patch which adds the `canxl_eth` netdevice
* iproute2-7.4-canxl_eth.patch : iproute2 patch to configure `canxl_eth` netdevices
* out-of-tree : `canxl_eth` module for existing kernels (Linux 6.12 and later)
* canxl_eth_vxcan_test.sh : test `canxl_eth` without CAN XL hardware (vxcan and network namespaces)
* [INSTALL.md](INSTALL.md) : integrate, build and install the patches for Linux and iproute2
* [OUT-OF-TREE.md](OUT-OF-TREE.md) : build and install the `canxl_eth` module for an existing (e.g. Debian) kernel
* [SETUP.md](SETUP.md) : set up a CAN XL Ethernet with the `ip` tool (SDT 0x04/0x05, IPv4/IPv6, point-to-point)
* [CONCEPT.md](CONCEPT.md) : concept of the `canxl_eth` netdevice in the Linux network layer

### Status

* Linux patch for Linux 7.4 (applies to Linux 7.4-rc4)
* iproute2 patch based on the iproute2 main branch for the upcoming iproute2 7.4
* out-of-tree build of the `canxl_eth` module for Linux 6.12 and later (e.g. Debian 13) with DKMS support

---
_Documented by Claude Opus 5.5_
