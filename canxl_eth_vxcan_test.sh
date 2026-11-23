#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# canxl_eth_vxcan_test.sh - test canxl_eth without CAN XL hardware
#
# Two sides are connected with a vxcan pair which acts as a virtual CAN XL
# bus. On each side canxl_eth devices are created on top of the vxcan
# interface and IPv4/IPv6 traffic is sent between the two sides.
#
# Default: both sides in the network namespaces xlA and xlB
#
#   xlA                                            xlB
#   xleth0 (SDT 05h, 192.168.42.1)       xleth0 (SDT 05h, 192.168.42.2)
#   xleth1 (SDT 04h, VCID 0x42)          xleth1 (SDT 04h, VCID 0x42)
#   xlp2p  (point-to-point, 10.0.0.1)    xlp2p  (point-to-point, 10.0.0.2)
#      |                                      |
#   vxcanA  <-------- virtual CAN XL -------->  vxcanB
#
# Option -r: side A in the root network namespace (e.g. for Wireshark)
#
#   root namespace                                 xlB
#   eth_xltest0 (SDT 05h, 192.168.42.1)  xleth0 (SDT 05h, 192.168.42.2)
#   eth_xltest1 (SDT 04h, VCID 0x42)     xleth1 (SDT 04h, VCID 0x42)
#   eth_xltest2 (point-to-point)         xlp2p  (point-to-point, 10.0.0.2)
#      |                                      |
#   vxcan_xltest0 <------ virtual CAN XL ---->  vxcanB
#
# Usage: canxl_eth_vxcan_test.sh [ -r ] [ -k ] [ cleanup ]
#
#   -r      : create side A in the root network namespace. The script waits
#             after the creation of the interfaces (when started in a
#             terminal), e.g. to start a capture in Wireshark.
#   -k      : keep the setup after the tests for manual tests, e.g.
#             ip netns exec xlA ping 192.168.42.2
#             ip netns exec xlB candump -x any
#   cleanup : remove the setup
#
# The 'ip' tool with canxl_eth support can be selected with the IP variable:
#
#   IP=../iproute2/ip/ip ./canxl_eth_vxcan_test.sh
#
# Author Oliver Hartkopp
#

IP=${IP:-ip}
NS_A=xlA
NS_B=xlB
ROOT=0
KEEP=0
FAILED=0

# vxcan interface of side A in the root namespace (option -r)
ROOT_CAN=vxcan_xltest0

# MTU of a CAN XL interface (sizeof(struct canxl_frame))
CANXL_MTU=2060

usage() {
	echo "Usage: $0 [ -r ] [ -k ] [ cleanup ]"
	echo "  -r      : create side A in the root network namespace"
	echo "            ($ROOT_CAN, eth_xltest0, eth_xltest1, eth_xltest2)"
	echo "  -k      : keep the setup after the tests"
	echo "  cleanup : remove the setup"
	echo "Select the 'ip' tool with canxl_eth support with IP=<path to ip>"
	exit 1
}

cleanup() {
	$IP link del $ROOT_CAN 2>/dev/null
	$IP netns del $NS_A 2>/dev/null
	$IP netns del $NS_B 2>/dev/null
}

# ip command in a namespace (empty namespace: root namespace)
ipns() {
	local ns="$1"
	shift

	if [ -n "$ns" ]; then
		$IP -n "$ns" "$@"
	else
		$IP "$@"
	fi
}

# run a command on side A
side_a() {
	if [ -n "$A_NS" ]; then
		$IP netns exec "$A_NS" "$@"
	else
		"$@"
	fi
}

fail() {
	echo "FAIL: $*"
	FAILED=1
}

# run a test command and report the result
check() {
	local name="$1"
	shift

	if "$@" >/dev/null 2>&1; then
		echo "PASS: $name"
	else
		fail "$name"
	fi
}

# wait until a device in a namespace has a carrier (LOWER_UP)
wait_carrier() {
	local ns="$1" dev="$2"

	for _ in $(seq 1 50); do
		if ipns "$ns" link show dev "$dev" 2>/dev/null | grep -q LOWER_UP; then
			return 0
		fi
		sleep 0.1
	done
	return 1
}

# create a canxl_eth device with addresses in a namespace
# canxl_eth_add <ns> <can dev> <name> <canxl_eth options> -- <ip addr add arguments> ...
canxl_eth_add() {
	local ns="$1" can="$2" name="$3"
	local opts=() addr=()

	shift 3
	while [ $# -gt 0 ] && [ "$1" != "--" ]; do
		opts+=("$1")
		shift
	done
	[ "$1" = "--" ] && shift

	ipns "$ns" link add link "$can" name "$name" type canxl_eth "${opts[@]}" || return 1
	ipns "$ns" link set "$name" up || return 1

	while [ $# -gt 0 ]; do
		addr=()
		while [ $# -gt 0 ] && [ "$1" != "--" ]; do
			addr+=("$1")
			shift
		done
		[ "$1" = "--" ] && shift
		ipns "$ns" addr add "${addr[@]}" dev "$name" || return 1
	done
}

while [ $# -gt 0 ]; do
	case "$1" in
	-r)
		ROOT=1
		;;
	-k)
		KEEP=1
		;;
	cleanup)
		cleanup
		exit 0
		;;
	*)
		usage
		;;
	esac
	shift
done

# side A: namespace (empty for the root namespace) and interface names
if [ $ROOT = 1 ]; then
	A_NS=""
	A_NAME="root namespace"
	A_CAN=$ROOT_CAN
	A_ETH0=eth_xltest0
	A_ETH1=eth_xltest1
	A_P2P=eth_xltest2
else
	A_NS=$NS_A
	A_NAME="namespace $NS_A"
	A_CAN=vxcanA
	A_ETH0=xleth0
	A_ETH1=xleth1
	A_P2P=xlp2p
fi

# side B: always in the namespace xlB
B_NS=$NS_B
B_CAN=vxcanB
B_ETH0=xleth0
B_ETH1=xleth1
B_P2P=xlp2p

if [ "$(id -u)" != "0" ]; then
	echo "Please run as root."
	exit 1
fi

if ! $IP link help canxl_eth 2>&1 | grep -q "type canxl_eth"; then
	echo "'$IP' does not support canxl_eth (see INSTALL.md)."
	echo "Select the patched ip tool with IP=<path to ip>, e.g."
	echo "IP=../iproute2/ip/ip $0"
	exit 1
fi

for mod in can vxcan canxl_eth; do
	if ! modprobe $mod 2>/dev/null && ! grep -qs "^$mod " /proc/modules; then
		echo "Unable to load the module '$mod'."
		[ $mod = canxl_eth ] && echo "Install canxl_eth (see INSTALL.md or OUT-OF-TREE.md) or load it with insmod."
		exit 1
	fi
done

# remove an old setup
cleanup

if [ $KEEP = 0 ]; then
	trap cleanup EXIT
fi

echo "Creating side A ($A_NAME) and side B (namespace $B_NS) with the vxcan pair $A_CAN/$B_CAN"

[ -n "$A_NS" ] && { $IP netns add "$A_NS" || exit 1; }
$IP netns add "$B_NS" || exit 1
$IP link add "$A_CAN" type vxcan peer name "$B_CAN" || exit 1

# vxcan only allows to change the MTU while the interface is down
$IP link set "$A_CAN" mtu $CANXL_MTU || exit 1
$IP link set "$B_CAN" mtu $CANXL_MTU || exit 1
[ -n "$A_NS" ] && { $IP link set "$A_CAN" netns "$A_NS" || exit 1; }
$IP link set "$B_CAN" netns "$B_NS" || exit 1

[ -n "$A_NS" ] && ipns "$A_NS" link set lo up
ipns "$B_NS" link set lo up
ipns "$A_NS" link set "$A_CAN" up || exit 1
ipns "$B_NS" link set "$B_CAN" up || exit 1

echo "Creating the canxl_eth devices"

# SDT 05h with IPv4/IPv6 (priority IDs 0x200 .. 0x2FF)
canxl_eth_add "$A_NS" "$A_CAN" "$A_ETH0" sdt 5 tx_prio 0x201 rx_prio 0x200 rx_prio_mask 0x700 \
	-- 192.168.42.1/24 -- fd00:611::1/64 nodad || exit 1
canxl_eth_add "$B_NS" "$B_CAN" "$B_ETH0" sdt 5 tx_prio 0x202 rx_prio 0x200 rx_prio_mask 0x700 \
	-- 192.168.42.2/24 -- fd00:611::2/64 nodad || exit 1

# SDT 04h with VCID 0x42 (priority IDs 0x300 .. 0x3FF)
canxl_eth_add "$A_NS" "$A_CAN" "$A_ETH1" sdt 4 tx_prio 0x301 tx_vcid 0x42 rx_prio 0x300 \
	rx_prio_mask 0x700 rx_vcid 0x42 rx_vcid_mask 0xff \
	-- 192.168.43.1/24 -- fd00:611:42::1/64 nodad || exit 1
canxl_eth_add "$B_NS" "$B_CAN" "$B_ETH1" sdt 4 tx_prio 0x302 tx_vcid 0x42 rx_prio 0x300 \
	rx_prio_mask 0x700 rx_vcid 0x42 rx_vcid_mask 0xff \
	-- 192.168.43.2/24 -- fd00:611:42::2/64 nodad || exit 1

# point-to-point with single priority ID filters
canxl_eth_add "$A_NS" "$A_CAN" "$A_P2P" sdt 5 tx_prio 0x111 rx_prio 0x112 rx_prio_mask 0x7ff \
	-- 10.0.0.1 peer 10.0.0.2 -- fd00:611:1::1/127 nodad || exit 1
canxl_eth_add "$B_NS" "$B_CAN" "$B_P2P" sdt 5 tx_prio 0x112 rx_prio 0x111 rx_prio_mask 0x7ff \
	-- 10.0.0.2 peer 10.0.0.1 -- fd00:611:1::/127 nodad || exit 1

for dev in "$A_ETH0" "$A_ETH1" "$A_P2P"; do
	wait_carrier "$A_NS" "$dev" || fail "carrier of $dev ($A_NAME)"
done
for dev in "$B_ETH0" "$B_ETH1" "$B_P2P"; do
	wait_carrier "$B_NS" "$dev" || fail "carrier of $dev (namespace $B_NS)"
done

echo
ipns "$A_NS" -d link show dev "$A_ETH0"
echo

# wait for the start of a capture on the interfaces in the root namespace
if [ $ROOT = 1 ] && [ -t 0 ]; then
	echo "The interfaces of side A are available in the root namespace:"
	echo "  $A_CAN (CAN XL), $A_ETH0 (SDT 05h), $A_ETH1 (SDT 04h/VCID), $A_P2P (point-to-point)"
	echo "Start a capture on these interfaces now (e.g. with Wireshark)."
	read -r -p "Press Enter to run the tests ... "
	echo
fi

echo "Running the tests"

check "SDT 05h IPv4 ping" side_a ping -c 3 -i 0.2 -W 2 192.168.42.2
check "SDT 05h IPv6 ping" side_a ping -6 -c 3 -i 0.2 -W 2 fd00:611::2
check "SDT 04h/VCID IPv4 ping" side_a ping -c 3 -i 0.2 -W 2 192.168.43.2
check "SDT 04h/VCID IPv6 ping" side_a ping -6 -c 3 -i 0.2 -W 2 fd00:611:42::2
check "point-to-point IPv4 ping" side_a ping -c 3 -i 0.2 -W 2 10.0.0.2
check "point-to-point IPv6 ping" side_a ping -6 -c 3 -i 0.2 -W 2 fd00:611:1::

# large frames with the maximum MTU (2034) without fragmentation
ipns "$A_NS" link set "$A_ETH0" mtu 2034
ipns "$B_NS" link set "$B_ETH0" mtu 2034
check "SDT 05h IPv4 ping MTU 2034 (2006 bytes payload)" \
	side_a ping -c 3 -i 0.2 -W 2 -M "do" -s 2006 192.168.42.2

# a frame which exceeds the MTU must not pass without fragmentation
if side_a ping -c 1 -W 1 -M "do" -s 2007 192.168.42.2 >/dev/null 2>&1; then
	fail "SDT 05h IPv4 ping exceeding MTU 2034 is rejected"
else
	echo "PASS: SDT 05h IPv4 ping exceeding MTU 2034 is rejected"
fi

echo
echo "Statistics of $A_ETH0 ($A_NAME):"
ipns "$A_NS" -s link show dev "$A_ETH0"

echo
if [ $FAILED = 0 ]; then
	echo "All tests passed."
else
	echo "Some tests FAILED."
fi

if [ $KEEP = 1 ]; then
	echo
	echo "The setup is kept. Remove it with: $0 cleanup"
fi

exit $FAILED
