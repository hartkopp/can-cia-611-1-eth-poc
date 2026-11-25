# CiA 611-1 SDT 0x04 and SDT 0x05

CiA 611-1 (CAN XL higher-layer functions - Part 1: Service data unit types)
specifies two service data unit types (SDT) to transport IEEE 802.3 MAC
frames (Ethernet frames) in CAN XL frames:

* SDT 0x04: IEEE 802.3 (MAC frame)
* SDT 0x05: IEEE 802.3 (MAC frame) extended

Both SDTs transport the same content: the Ethernet frame without the frame
check sequence (FCS, the 32 bit CRC at the end of an Ethernet frame) in the
CAN XL data field. They only differ in the content
of the CAN XL frame fields VCID and AF.

## Common content

The CAN XL data field contains the IEEE 802.3 MAC frame (destination address,
source address, optional VLAN tags, EtherType/length and MAC client data)
without the FCS:

```
| DA (6) | SA (6) | [ VLAN tag (4) ] | EtherType (2) | MAC client data ... |
```

* The CAN XL frame has its own CRCs (preface CRC and frame CRC), so the FCS
  (frame check sequence) of the Ethernet frame is not transported.
* The minimum Ethernet frame length is not relevant for CAN XL. A CAN XL node
  which is the origin of the Ethernet frame can omit the padding.
* The CAN XL data field allows up to 2048 bytes. The MTU has to be set up by
  the system designer so that the Ethernet frames fit into the CAN XL data
  field of all nodes (in `canxl_eth`: max. MTU 2034 = 2048 - 14 bytes
  Ethernet header).

## SDT 0x04: IEEE 802.3 (MAC frame)

| CAN XL frame field | Content |
|---|---|
| VCID | not in the scope of CiA 611-1 |
| AF | not in the scope of CiA 611-1 |
| data | Ethernet frame without FCS |

SDT 0x04 is the plain transport of Ethernet frames. The content of the VCID
and AF fields is not defined by CiA 611-1 and can be used by the system
designer, e.g. the VCID to separate virtual CAN networks.

`canxl_eth` uses for SDT 0x04:

* VCID: the configured `tx_vcid` (default 0x00)
* AF: `0xCCCCCCCC` (`CANXL_ETH_SDT04_AF_PAD`)

The AF content of SDT 0x04 is undefined. `canxl_eth` fills it with the
pattern 0xCC (binary 11001100), which is commonly used for currently unused
content, like for the mandatory padding of CAN FD frames (e.g. the default
padding content of ISO 15765-2 (ISO-TP) in Linux) and the `CCh` dummy byte in
CiA 611-1 (SDT 0x06/0x07). In CAN CC and CAN FD frames this pattern with its
bit flips prevents additional stuff bits. In CAN XL frames the AF is part of
the data phase with fixed stuff bits, so the pattern has no influence on the
stuff bits there.

## SDT 0x05: IEEE 802.3 (MAC frame) extended

| CAN XL frame field | Content |
|---|---|
| VCID | lower 8 bit of the VID of the first VLAN tag (C-VLAN 0x8100 or S-VLAN 0x88A8), otherwise 0x00 |
| AF | 1st, 4th, 5th and 6th octet of the destination MAC address |
| data | Ethernet frame without FCS |

The AF contains parts of the destination MAC address (MSB first):

```
 31            24 23            16 15             8 7              0
+----------------+----------------+----------------+----------------+
| 1st octet DA   | 4th octet DA   | 5th octet DA   | 6th octet DA   |
+----------------+----------------+----------------+----------------+
```

Example from CiA 611-1 (Annex A.2): An Ethernet frame with the destination
address `01:23:45:67:89:AB` and a C-VLAN tag with the TCI `80:42` is sent with
the AF `01:67:89:AB` and the VCID `0x42`.

### The idea behind SDT 0x05: acceptance filtering in hardware

Some CAN XL controllers provide acceptance filters with 64 bits which cover
the CAN XL frame fields from the priority ID up to the AF:

```
| priority ID (11) | flags | SDT (8) | VCID (8) | AF (32) |
```

With SDT 0x05 such a controller can filter Ethernet frames without looking into
the CAN XL data field:

* **Destination address:** The AF contains the octets of the destination MAC
  address which are most relevant for the filtering. The 1st octet contains
  the I/G bit (individual/group address: unicast or multicast/broadcast) and
  the U/L bit. The 4th to 6th octets are the device specific part of the
  address. The 2nd and 3rd octets are usually part of the organizationally
  unique identifier (OUI) of the manufacturer and are omitted. So the AF
  allows to accept the unicast frames for the own MAC address and the
  broadcast/multicast frames in hardware.
* **VLAN:** The VCID contains the lower 8 bits of the VLAN ID. A CAN XL
  controller can separate VLANs with a VCID filter without parsing the VLAN
  tag in the CAN XL data field.

This is especially useful for CAN XL nodes with limited resources (e.g.
microcontrollers), which would otherwise have to receive and inspect every
Ethernet frame in software.

## SDT 0x04 and SDT 0x05 in canxl_eth

* **Receive path:** Linux does not use hardware acceptance filters. The PF_CAN
  network layer filters in software and `canxl_eth` only filters on the
  priority ID and the VCID (`rx_prio`, `rx_prio_mask`, `rx_vcid`,
  `rx_vcid_mask`). The AF is not checked. CAN XL frames with SDT 0x04 and
  SDT 0x05 are received in the same way, independent of the configured `sdt`.
  The Ethernet stack decides whether a frame is addressed to the host.
* **Transmit path:** The configured `sdt` defines the content of the VCID and
  AF fields of the sent CAN XL frames. With SDT 0x05 the AF and the VCID are
  filled as specified, so that other CAN XL nodes with hardware acceptance
  filters on the same CAN XL bus can filter the Ethernet frames.

Recommendation:

* **SDT 0x05** for mixed networks, where other CAN XL nodes (e.g.
  microcontrollers) filter the Ethernet traffic in hardware.
* **SDT 0x04** when the Ethernet nodes are separated by the priority ID or a
  fixed VCID anyway, e.g. for a point-to-point connection.

---
_Documented by Claude Opus 5.5_
