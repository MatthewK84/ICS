#!/usr/bin/env bash
# An emulated network TAP for the SITL rig (ICS-018).
#
# Usage: tap.sh up CONTAINER...
#        tap.sh capture PCAP
#        tap.sh down
#
# "up" creates the TAP's monitor port, ics-tap0, in its own network namespace
# (ics-tap), as if it were a separate capture machine cabled to a hardware
# TAP. It is one end of a veth pair; the kernel copies every frame each
# container sends or receives into the other end, ics-tap0-feed, with
# nftables' dup statement on the host side of the container's network link,
# at both its ingress and egress hooks. Like a hardware TAP the copy is
# passive: the autopilots' traffic is neither delayed nor changed, and nothing
# reaches the network from the monitor port, because the host drops whatever
# comes back through ics-tap0-feed. The copies never reach the host's own
# network stack either, which would otherwise forward them back onto the
# range.
#
# "capture" runs tcpdump on ics-tap0 for UDP until interrupted, writing PCAP
# as the calling user. "down" removes the namespace, the veth pair and the
# nftables table. Needs root, ip (iproute2), nft (nftables) and tcpdump.
set -euo pipefail

readonly NAMESPACE="ics-tap"
readonly TAP="ics-tap0"
readonly FEED="ics-tap0-feed"
readonly TABLE="ics_tap"

host_link() {
  local container="$1" index
  index="$(docker exec "${container}" cat /sys/class/net/eth0/iflink)"
  ip -o link | awk -F': ' -v want="${index}" '$1 == want { split($2, name, "@"); print name[1] }'
}

mirror() {
  local container="$1" link chain hook
  link="$(host_link "${container}")"
  if [[ -z "${link}" ]]; then
    echo "error: no host link found for ${container}" >&2
    exit 1
  fi
  for hook in ingress egress; do
    chain="${container//-/_}_${hook}"
    nft add chain netdev "${TABLE}" "${chain}" "{ type filter hook ${hook} device ${link} priority 0; }"
    nft add rule netdev "${TABLE}" "${chain}" dup to "${FEED}"
  done
  echo "${container}: ${link} mirrored to ${TAP}"
}

monitor_port() {
  ip netns add "${NAMESPACE}"
  ip link add "${TAP}" type veth peer name "${FEED}"
  ip link set "${TAP}" netns "${NAMESPACE}"
  # No IPv6 on either end, so neither sends router or neighbour solicitations.
  if [[ -d /proc/sys/net/ipv6 ]]; then
    ip netns exec "${NAMESPACE}" sysctl -qw "net.ipv6.conf.${TAP}.disable_ipv6=1"
    sysctl -qw "net.ipv6.conf.${FEED}.disable_ipv6=1"
  fi
  ip netns exec "${NAMESPACE}" ip link set "${TAP}" up
  ip link set "${FEED}" up
  nft add table netdev "${TABLE}"
  nft add chain netdev "${TABLE}" monitor_return "{ type filter hook ingress device ${FEED} priority 0; policy drop; }"
}

up() {
  monitor_port
  local container
  for container in "$@"; do
    mirror "${container}"
  done
}

capture() {
  local pcap="$1"
  exec ip netns exec "${NAMESPACE}" tcpdump -i "${TAP}" -U -n -Z "${SUDO_USER:-root}" -w "${pcap}" udp
}

down() {
  if nft list table netdev "${TABLE}" >/dev/null 2>&1; then
    nft delete table netdev "${TABLE}"
  fi
  if ip link show "${FEED}" >/dev/null 2>&1; then
    ip link delete "${FEED}"
  fi
  if ip netns list | grep -qx "${NAMESPACE}\( .*\)\?"; then
    ip netns delete "${NAMESPACE}"
  fi
}

case "${1:-}" in
  up)
    shift
    up "$@"
    ;;
  capture)
    capture "${2:?usage: tap.sh capture PCAP}"
    ;;
  down)
    down
    ;;
  *)
    echo "usage: tap.sh up CONTAINER... | tap.sh capture PCAP | tap.sh down" >&2
    exit 2
    ;;
esac
