# IPv4 port triggering

A `port_trigger` section grants temporary incoming TCP/UDP ports to the first
internal IPv4 address that sends permitted outbound trigger traffic through the
external zone. Incoming ports are forwarded unchanged. A rule may open TCP,
UDP, or both, independently of the outbound protocol.

```uci
config port_trigger
        option name 'Application'
        option enabled '1'
        option src 'lan'
        option dest 'wan'
        option trigger_proto 'tcp'
        option trigger_port '4000-4002'
        option open_proto 'tcpudp'
        option open_port '5000-5010'
        option timeout '600'
```

Both zones must support IPv4 and must differ. Ports range from 1 to 65535;
timeout is an integer from 1 to 86400 seconds. Ordinary forwarding policy must permit
the outbound traffic. No trigger grants outbound access by itself.

Each rule and external device has one owner. Only matching original-direction
outbound packets from that owner renew the inactivity timeout. A competing host
cannot replace an active owner or refresh it merely by sending a trigger. Claim
and renewal use a timed kernel map; a concurrent expiry/reclaim can refresh the
new owner's timer but does not replace its address. There are at most 4096 live
rule/device leases. Trigger flows stay outside flow offload in both directions,
even before acquisition. Opened connections can use normal flow offload.

A full firewall reload clears all trigger leases and replaces reservations in
one nftables transaction, including after the last section is removed. Interface
down events reload the firewall when triggers are configured. Existing conntrack
entries can continue after a lease expires or is withdrawn; new connections need
an active lease or an independently configured forwarding policy.

Specific port-forward and trigger allocations on overlapping external devices
cannot overlap for the same protocol. Conflicting trigger sections are skipped
with a firewall diagnostic. Trigger DNAT precedes portless forwarding (DMZ),
which remains an independent fallback when a trigger is inactive. Reservations
are per external device and protocol, including when the trigger has no owner.
For subnet-constrained zones, reservations conservatively cover the matching
external devices; the packet rules still honor the complete zone predicates.
Reservations and packet rules share the device snapshot from the last firewall
reload. Newly created devices, including wildcard matches, participate after
the next reload publishes both together.

This implementation requires nftables with the scalar-map binary operand patch
(1.1.6-r9 in this tree). Install miniupnpd-nftables 2.3.9-r8 or later together with
this firewall build if UPnP/NAT-PMP/PCP is used. That daemon consults the committed
reservation set before creation and renewal, and serializes allocation with fw4
reload using `/var/run/fw4.lock`. One snapshot covers alternate-port searches;
a missing set is compatible with ordinary fw4, while other lookup failures deny
allocation. The reserved set is `inet fw4 port_trigger_ports`, with interval keys
`ifname . inet_proto . inet_service`. Lease-file restoration uses the same checks.

LuCI exposes these sections under **Network > Firewall > Port Triggers**.

## Other per-packet policies

The `flow_offload` chain supports the standard nftables `chain-pre` and
`chain-post` include positions. Use `chain-pre` with `return` to keep selected
traffic on the regular packet path. A forward-chain include runs after the
offload attempt and cannot prevent that packet from installing a flow entry.
The post hook is suitable for observing packets that reach the end of the chain;
it does not undo flow insertion.

For example, a source-address binding enforced in `raw_prerouting` must exclude
the protected address from offload as both source and destination, regardless of
the received MAC address. Otherwise a reply or a packet with the wrong MAC could
install a shortcut around the binding check. Match the wrong MAC in the early
filter itself. A DHCP reservation chooses an address; a permanent netifd neighbor
protects the router's neighbor entry; neither replaces that packet filter.

Use the existing `chain-post` include position for `raw_prerouting`. A full fw4
reload deletes the previous flowtable, so enabling a per-packet policy also
retires existing software shortcuts. These controls govern traffic processed by
the router and do not imply isolation between hardware-switched LAN peers.
