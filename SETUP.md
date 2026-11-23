# Setup of a CAN XL Ethernet

This document shows how to set up Ethernet over CAN XL with the `ip` tool
after the patched Linux kernel and iproute2 have been built and installed
(see [INSTALL.md](INSTALL.md)).

When iproute2 has not been installed, replace `ip` with the build artifact
`./ip/ip` in the iproute2 source directory for the `canxl_eth` specific
commands (`ip link add/set ... type canxl_eth` and `ip -d link show`).

## Test without CAN XL hardware

The script `canxl_eth_vxcan_test.sh` tests `canxl_eth` on a single host without
CAN XL hardware. Two network namespaces (`xlA`, `xlB`) are connected with a
`vxcan` pair which acts as a virtual CAN XL bus. The script creates the
`canxl_eth` devices of the examples below in both namespaces and tests the
IPv4/IPv6 connectivity (SDT 0x05, SDT 0x04 with VCID, point-to-point and the
maximum MTU):

```
sudo IP=../iproute2/ip/ip ./canxl_eth_vxcan_test.sh
```

The `IP` variable selects the `ip` tool with `canxl_eth` support (see
[INSTALL.md](INSTALL.md)). With the option `-k` the setup is kept after the
tests for own experiments, e.g.:

```
sudo ip netns exec xlA ping 192.168.42.2
sudo ip netns exec xlB candump -x any
```

Remove the setup with `sudo ./canxl_eth_vxcan_test.sh cleanup`.

### Capture the traffic in a network namespace

The interfaces in the namespaces `xlA` and `xlB` are not visible for tools in
the root namespace (e.g. Wireshark or `candump`). The traffic can be captured
inside of a namespace with `dumpcap` (from the Wireshark package) and passed
to Wireshark in the root namespace with a pipe. Wireshark runs as a regular
user and shows the CAN XL and the Ethernet traffic in one capture (pcapng):

```
sudo ip netns exec xlA dumpcap -q -i vxcanA -i xleth0 -i xleth1 -i xlp2p -w - | wireshark -k -i -
```

As the vxcan pair is a CAN XL bus with two nodes, the capture on `vxcanA`
contains the CAN XL traffic of both nodes: the received frames and the sent
frames (independent of the `echo` setting). The capture on `xleth0`, `xleth1`
and `xlp2p` contains the same traffic as Ethernet frames.

`candump` can be run inside of a namespace too:

```
sudo ip netns exec xlA candump -x any
```

### Side A in the root namespace

With the option `-r` side A is created in the root namespace with the
interfaces `vxcan_xltest0` (CAN XL), `eth_xltest0` (SDT 0x05), `eth_xltest1`
(SDT 0x04 with VCID) and `eth_xltest2` (point-to-point). Side B remains in the
namespace `xlB`. The interfaces of side A can be selected directly in
Wireshark or used with `candump`:

```
sudo IP=../iproute2/ip/ip ./canxl_eth_vxcan_test.sh -r
```

After the creation of the interfaces the script waits for the Enter key (when
started in a terminal), so that a capture can be started on these interfaces
before the tests are run. With `-r -k` the interfaces are kept in the root
namespace after the tests.

Note: With `-r` the test addresses (e.g. 192.168.42.1/24) are configured in
the root namespace. Firewall rules of the host may affect the tests.

## The CAN XL interface

The `canxl_eth` device is created on top of a CAN XL interface (here `can0`)
which has to be configured for CAN XL and set up. Example:

```
ip link set can0 type can bitrate 500000 dbitrate 4000000 fd on xbitrate 8000000 xl on
ip link set can0 up
```

The bit timing values are just an example and depend on the CAN XL network
(see `ip link help can`). With enabled CAN XL the CAN interface has the
CAN XL MTU of 2060 bytes (`ip link show can0`).

The carrier of the `canxl_eth` device follows the CAN XL interface: It is on
when the CAN XL interface is up, has carrier and is configured for CAN XL.

## canxl_eth settings

```
ip link add link CANDEV name NAME type canxl_eth
        sdt { 4 | 5 } tx_prio PRIO rx_prio PRIO rx_prio_mask PRIO_MASK
        [ tx_vcid VCID ] [ rx_vcid VCID ] [ rx_vcid_mask VCID_MASK ]
        [ echo { on | off } ] [ sec { on | off } ] [ rrs { on | off } ]
```

| Setting | Description | Default |
|---|---|---|
| `sdt` | SDT of the sent CAN XL frames (SDT 0x04 and 0x05 are received) | mandatory |
| `tx_prio` | 11 bit CAN XL priority ID of the sent frames | mandatory |
| `rx_prio` / `rx_prio_mask` | receive filter for the 11 bit priority ID | mandatory |
| `tx_vcid` | VCID of the sent frames (only SDT 0x04) | 0x00 |
| `rx_vcid` / `rx_vcid_mask` | receive filter for the VCID | 0x00 / 0x00 (all VCIDs) |
| `echo` | local echo of the sent frames to PF_CAN users (e.g. `candump`) | on |
| `sec` / `rrs` | SEC/RRS value of the sent frames and required SEC/RRS value of the received frames | off / off |

* Like the CAN identifiers of Classical CAN and CAN FD the CAN XL priority ID
  has to be **unique for each sender** on the CAN bus. Each node gets its own
  `tx_prio`.
* SDT 0x04: the acceptance field (AF) is 0xCCCCCCCC and the VCID is `tx_vcid`.
* SDT 0x05: the AF contains the 1st, 4th, 5th and 6th octet of the destination
  MAC address and the VCID contains the lower 8 bit of the VLAN ID of a VLAN
  tag (or 0x00 without VLAN tag).
* The settings can only be changed while the `canxl_eth` device is down. When
  one of `echo`, `sec` or `rrs` is given, all three flags are set (the
  unspecified flags get their default).

Show the settings:

```
ip -d link show xleth0
```

```
... canxl_eth sdt 5 tx_prio 0x201 rx_prio 0x200 rx_prio_mask 0x700 tx_vcid 0x00 rx_vcid 0x00 rx_vcid_mask 0x00 echo on sec off rrs off ...
```

## Example 1: SDT 0x05 with IPv4

Two (or more) nodes on a CAN XL bus. All Ethernet nodes use priority IDs in
the range 0x200 .. 0x2FF. The receive filter `rx_prio 0x200 rx_prio_mask 0x700`
accepts this range, so that other CAN XL traffic is not processed by the
`canxl_eth` device at all.

Node A:

```
ip link add link can0 name xleth0 type canxl_eth sdt 5 tx_prio 0x201 rx_prio 0x200 rx_prio_mask 0x700
ip link set xleth0 up
ip addr add 192.168.42.1/24 dev xleth0
```

Node B:

```
ip link add link can0 name xleth0 type canxl_eth sdt 5 tx_prio 0x202 rx_prio 0x200 rx_prio_mask 0x700
ip link set xleth0 up
ip addr add 192.168.42.2/24 dev xleth0
```

Test on node A:

```
ping 192.168.42.2
```

### VLAN on top of SDT 0x05

With SDT 0x05 the VCID is derived from the VLAN ID. A VLAN device with VLAN ID
42 (0x2A) on top of `xleth0` sends its Ethernet frames with VCID 0x2A:

```
ip link add link xleth0 name xleth0.42 type vlan id 42
ip link set xleth0.42 up
ip addr add 192.168.142.1/24 dev xleth0.42
```

## Example 2: SDT 0x04 with VCID and IPv6

A separate Ethernet with SDT 0x04 in the virtual CAN network with VCID 0x42.
The priority IDs are in the range 0x300 .. 0x3FF and only frames with VCID
0x42 are received.

Node A:

```
ip link add link can0 name xleth1 type canxl_eth sdt 4 tx_prio 0x301 tx_vcid 0x42 rx_prio 0x300 rx_prio_mask 0x700 rx_vcid 0x42 rx_vcid_mask 0xff
ip link set xleth1 up
ip -6 addr add fd00:611:42::1/64 dev xleth1
```

Node B:

```
ip link add link can0 name xleth1 type canxl_eth sdt 4 tx_prio 0x302 tx_vcid 0x42 rx_prio 0x300 rx_prio_mask 0x700 rx_vcid 0x42 rx_vcid_mask 0xff
ip link set xleth1 up
ip -6 addr add fd00:611:42::2/64 dev xleth1
```

Test on node A:

```
ping -6 fd00:611:42::2
```

The IPv6 link-local addresses (`fe80::/64`) are created automatically when
the `canxl_eth` device is set up and can be used as well
(e.g. `ping -6 fe80::...%xleth1`).

## Example 3: point-to-point with IPv4 and IPv6

Two nodes which only receive the CAN XL frames of each other: The receive
filter of each node matches exactly the `tx_prio` of the peer
(`rx_prio_mask 0x7ff`). Without a VCID filter this single priority ID filter
uses the fast path of the PF_CAN receive filters.

Node A:

```
ip link add link can0 name xlp2p type canxl_eth sdt 5 tx_prio 0x111 rx_prio 0x112 rx_prio_mask 0x7ff
ip link set xlp2p up
ip addr add 10.0.0.1 peer 10.0.0.2 dev xlp2p
ip -6 addr add fd00:611:1::1/127 dev xlp2p
```

Node B:

```
ip link add link can0 name xlp2p type canxl_eth sdt 5 tx_prio 0x112 rx_prio 0x111 rx_prio_mask 0x7ff
ip link set xlp2p up
ip addr add 10.0.0.2 peer 10.0.0.1 dev xlp2p
ip -6 addr add fd00:611:1::/127 dev xlp2p
```

Test on node A:

```
ping 10.0.0.2
ping -6 fd00:611:1::
```

## MTU

The Ethernet frame (without FCS) is the content of the CAN XL data field
(max. 2048 bytes). The default MTU is 1500 and the maximum MTU is 2034
(2048 - 14 bytes Ethernet header). With VLAN tags the MTU must not exceed
2030. All nodes of a CAN XL Ethernet should use the same MTU:

```
ip link set xleth0 mtu 2000
```

## Change settings

The settings can only be changed while the `canxl_eth` device is down:

```
ip link set xleth0 down
ip link set xleth0 type canxl_eth tx_prio 0x203
ip link set xleth0 up
```

## Monitor the traffic

With `echo on` (default) the sent CAN XL frames are echoed to the local PF_CAN
users. A `candump` with CAN XL support shows the received and the sent
Ethernet frames (e.g. for log files). Local PF_CAN applications (e.g.
`cansend`) can also send CAN XL frames with SDT 0x04/0x05 to the `canxl_eth`
devices on the same host.

Note: CAN_RAW sockets only forward CAN XL frames with a VCID other than 0x00
when the VCID receive filter is enabled on the socket (`CAN_RAW_XL_VCID_OPTS`
with `CAN_RAW_XL_VCID_RX_FILTER`).

Disable the echo:

```
ip link set xleth0 down
ip link set xleth0 type canxl_eth echo off
ip link set xleth0 up
```

## Remove

```
ip link del xleth0
```

A `canxl_eth` device is removed automatically when its CAN XL interface is
removed.

---
_Documented by Claude Opus 5.5_
