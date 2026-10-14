# GRX500 routed hardware assistance

The Ethernet parent integrates the GSWIP3 PAE route engine with the native
netfilter flowtable through `TC_SETUP_FT`. PAE performs tuple lookup, address and
port translation, MAC rewriting and TTL/hop-limit decrement. Completed packets
return through the existing WAN DMA receive owner and are transmitted with
`dev_queue_xmit()`. This is hardware routing/NAT assistance; it does not bypass
the CPU for final egress and makes no line-rate or CPU-utilization claim.

Both IPv4 and IPv6 TCP/UDP support plain routing and single-sided address/port
translation. Exact input, address and port matches and complete Ethernet
rewrite actions are required. Double NAT, encapsulation, VLAN actions, partial
address masks and unsupported flow actions return `-EOPNOTSUPP`. Tagged input,
IP options/extensions, fragmentation and routing exceptions do not match the
PAE admission classifier. The installed parser explicitly marks TCP FIN and
RST as routing exceptions, preserving their ordinary software path.

The caller's exact ingress ifindex remains the admission boundary. Resolving a
DSA port's bridge MAC does not authorize other bridge ingress ports. A redirect
to a bridged DSA port is completed through its current Linux bridge rather than
its cached physical jack. Linux therefore continues to own FDB movement, STP,
bridge isolation and final egress. VLAN-aware bridge paths remain software,
including the existing tag-preserving DSA fallback.

## Publication and retirement

One parent mutex serializes the shared GSW-R classification/route staging
registers. Referenced IP, MAC and MTU entries are installed before checked route
publication; the native MTU comparison uses the requested value plus one.
Hardware-table operations have bounded busy polling and publication readback.

MAC addresses use one shared, reference-counted table, with index zero reserved
for disabled rewriting. Initialization reads the native CAP14 source and
destination capacities and bounds each role by both its reported count and the
action field (8-bit source, 9-bit destination). A lease shares an identical
address only within the requesting role's range. New destination leases prefer
the destination-only range, preserving the smaller source-addressable prefix.
Each new row is written and read back before its index enters a session; session
publication also checks both MAC indices. Session/IP resource ownership remains
bounded at 120 directions independently of the number of distinct MAC addresses.
Actual MAC exhaustion returns a software fallback, without publishing a partial
action or changing the advertised hardware capacity.

An entry holds both MAC leases until checked session retirement and cookie
removal, including invalidation, final block unbind and publication rollback.
A failed hardware retirement pins its leases with the existing resources.
Pre-publication failure returns only leases already acquired by that attempt.
An unreferenced row is not exposed by a valid action; its next owner rewrites and
checks the data before publication. Lease indices remain immutable in the entry
until its existing RCU lifetime ends.

A dedicated internal destination endpoint supplies a PMAC-generated descriptor
marker on every flow-ID/class combination. Ordinary CPU misses retain marker
zero. Its FDMA egress port is enabled to fetch completed frames toward PMAC/RX
DMA; its SDMA ingress port remains disabled. Both enable states are read back
before hardware assistance is published. The returned session slot, 13-bit
destination subinterface and 8-bit flow
ID identify the current software generation; packet bytes cannot supply this
identity. The receive owner additionally checks actual family, protocol,
translated tuple, MAC addresses, ingress port, IP extent and current egress MTU.
A stale or malformed marked frame is dropped, never submitted to IP receive
for a second NAT or TTL operation.

Topology, address, neighbor and route changes close the software return gate
and invalidate both directions before releasing held Linux paths. Blocking
DSA changes retain the lock order `RTNL / DSA PCE lock -> parent flow lock`.
Atomic notifications close the gate before scheduling the native sleeping
table operation. RCU protects an acquired completion path; cookie statistics
remain available until the flowtable destroys that cookie.

The software flowtable retains the native route reference and IPv6 route cookie
for direct L2 output as well as neighbor output. Before rewriting a packet it
checks the current route and forwarding MTU; a changed route or MTU retires the
old tuple and returns the packet to normal routing. Hardware withdrawal alone
must not leave a stale direct software path bypassing the updated policy.
DIRECT software flows also validate current ingress and lower-output forwarding
state and cached bridge membership before NAT or TTL rewriting. Invalid state
retires the tuple and returns through the ordinary bridge path; bridge path
admission excludes nonforwarding ports.

Each native flow block owns its own callback context, shared by that block's
LAN and WAN device references. Atomic nftables replacement binds the next block
before unbinding the old one; both registrations may therefore coexist. A final
unbind closes only the retiring context's return gate, checks hardware removal
and releases its entries, cookies and resources. It does not advance the shared
epoch or retire the replacement block's entries. Cookie statistics remain
available while their callback is registered; native cleanup after callback
removal cannot be relied upon to issue another driver `FLOW_CLS_DESTROY`.
Callback and entry references keep the context alive through RCU retirement.
A failed hardware removal pins the entry, resources and context under the
existing global fault gate until parent teardown; callback removal is not
reported as successful hardware retirement.

The native routing-table transaction invalidates a session before referenced
tables are reused, matching the vendor API's deletion sequence. A new session
uses a new generation. Generations never wrap or restart on interface stop or
DMA recovery; only a full, acknowledged GSW reset permits a new namespace.
Logical ingress extension IDs are similarly not reassigned within a hardware
epoch. Resource or generation exhaustion leaves the flow in software. A table
transaction failure stops admission and rejects old returns; it is reported as
a hardware error, not successful removal or successful software recovery.

The initial bounded allocator has 120 directional resource owners, eight shared
MTU entries and 4096 hash heads. Hash collisions use software rather than an
unqualified collision-chain update. A conntrack hardware-offload flag alone
does not prove that both directions were admitted.

## Accounting and scheduling

`FLOW_CLS_STATS` reports actual completed IP packet/byte pairs from one coherent
writer snapshot. Bytes exclude Ethernet/PMAC headers and padding. This avoids
inventing a packet count from a byte-only route counter or assuming RMON byte
units. The boundary is accepted native egress, not transmission acknowledgement.

`ethtool -S wan` also exposes aggregate directional session and return counters:
`pae_active_sessions`, `pae_return_packets`, `pae_return_l3_bytes`,
`pae_stale_returns`, `pae_invalid_returns`, `pae_table_failures` and
`pae_capacity_fallbacks`. Wire identity/checksum, exception, invalidation and
capacity behavior require runtime qualification; source implementation alone
is not hardware acceptance.

Admission diagnostics count actual `FLOW_CLS_REPLACE` callbacks with
`pae_add_requests`, `pae_add_accepted` and `pae_add_rejected`. The last rejection
is retained in `pae_last_add_errno` (positive native errno),
`pae_last_add_stage`, `pae_last_add_iif` and `pae_last_add_oif`. Interface indexes
are the decoded exact input and raw redirect target, or zero if decoding did
not reach that field. Stage values are 1 state, 2 existing cookie, 3 allocation,
4 match, 5 action, 6 Linux path, 7 epoch, 8 classifier domain, 9 session resource,
10 MTU resource, 11 IP/MAC subtable and 12 session publication. These fields are
read and written under the existing flow mutex. Interpret the last-rejection
gauges only when the rejected count increases; they do not attribute concurrent
attempts to a particular packet or conntrack direction.

`pae_rx_examined` counts frames admitted by the existing WAN DMA descriptor
checks to the flow receive owner. `pae_rx_candidates` counts frames carrying
either the reserved endpoint or return marker, before validating both.
`pae_rx_unmarked_nat` counts other frames whose native descriptor NAT-result bit
is set; that bit alone does not prove a trusted or correctly translated return.
`pae_rx_validated` counts complete generation, tuple, header and current Linux
owner validation before skb allocation. `pae_tx_rejected` counts subsequent
`dev_queue_xmit()` failures under the existing `net_xmit_eval()` interpretation.
The existing success counter still means accepted Linux egress, not a wire ACK.
These independent receive counters complement physical-port RMON and packet
captures; an aggregate delta does not itself establish per-packet identity.
The observations do not change admission, hardware tables, forwarding or queue
policy.

The same stats request also performs the vendor's read-only session-hit
operation for at most 120 currently hardware-valid owned entries, under the
existing table mutex. `pae_hw_read_ok` requires every selected table read to
complete; it is false while the parent is stopping. `pae_hw_identity_ok` also
requires all active entries to match their current key, validity, slot and
generation. `pae_hw_matched_sessions` counts those matching entries, and
`pae_hw_hit_sessions` counts their native hit bits (session VAL12 bit15).
`pae_hw_counter_raw` sums their native 32-bit VAL10/VAL11 counters without
assigning packet or byte units. These are current-session gauges, not cumulative
software accounting; they can fall when an entry retires. `pae_hw_read_errors`
counts failed read transactions. Partial values remain visible on read or
identity failure and must not qualify an absence claim.

Interpret hit/counter progress only with both validity gauges true and matching
nonzero active/observed entry counts. No hits despite proved ingress narrows the
lookup/classifier boundary. Hits with no receive candidates narrow the later
route-action/return boundary. Neither result by itself proves a particular drop
action or successful translation. The getter follows
`GSW_ROUTE_SessHitOp(GSW_ROUTE_HIT_N_CNTR_READ)`; it neither clears hit/counter
state nor changes any entry, generation, reference, epoch or forwarding policy.

Return rejection diagnostics count the unchanged failure predicates separately:
`pae_rx_reject_descriptor`, `pae_rx_reject_ip_header`,
`pae_rx_reject_transport`, `pae_rx_reject_tuple`, `pae_rx_reject_mac`,
`pae_rx_reject_mtu` and `pae_rx_reject_alloc`. The corresponding
`pae_last_rx_stage` values are 1 through 7. Stale generation/path returns remain
in their existing separate counter.

Return-diagnostic scratch data is initialized only after a frame carries a
reserved return endpoint or marker and a flow owner exists. Ordinary CPU misses
retain their examined/unmarked-NAT accounting without clearing unused diagnostic
storage. Every marked-return validation path still starts with the same zeroed
fields and raw descriptor values before it can publish a rejection.

The `pae_last_rx_*` gauges are one coherent, bounded last-rejection snapshot,
copied with a spinlock. DW0/DW1 and frame length are the actual receive inputs;
IP header/length/checksum residue, protocol, ports and MAC/IP addresses are the
parsed header values, never packet payload. Unparsed fields remain zero.
`has_entry` marks whether expected metadata was available under the original
RCU lifetime; expected values describe the admitted translated tuple and L2
rewrite, resource, cached MTU and current Linux egress MTU. Ports are source in
the upper 16 bits and destination in the lower 16; MACs are 48-bit network-order
integers. IPv6 addresses are high/low 64-bit network-order halves; IPv4 uses the
low half. `ip_head` is the first four IP-header bytes, `ip_csum` the IPv4 header
checksum residue, and `l4_info` is UDP length/checksum or TCP data-offset/flags.
These are absolute gauges, not deltas or proof that all rejected packets shared
the last packet's cause. No table, forwarding, lifetime or acceptance predicate
is changed by this observation.

`pae_last_rx_smac_index` and `pae_last_rx_dmac_index` identify the rejected entry's
immutable leased indices when `has_entry` is set. The absolute MAC-owner gauges
`pae_mac_cap14`, `pae_mac_smac_capacity` and `pae_mac_dmac_capacity` report the
native capability word and decoded entry counts. `pae_mac_smac_limit` and
`pae_mac_dmac_limit` are exclusive index limits after action-width bounds;
index zero remains unavailable. `pae_mac_in_use` counts referenced distinct rows
and `pae_mac_references` counts all held source/destination leases. These gauges
are sampled under the existing flow mutex and do not clear hardware state.

## CPU-miss polling ownership

Each DMA pair attaches its NAPI instance to its real registered Ethernet device:
the LAN conduit owns DMA2 and WAN owns DMA1. Thread creation follows device
registration, so native `napi/<netdev>-<id>` names use the actual interface name.
The driver also records each NAPI instance's receive IRQ through the standard
kernel API. No private thread identifier or metadata interface is required.

OpenWrt packet steering treats these two GRX500 netdevs as independent service
owners even though they share a platform device. Its existing CPU allocator,
RPS modes, disabled-mode behavior and reload lifecycle remain in control; the
driver does not pin threads to fixed CPUs. This prevents the two independent
DMA pollers from being collapsed into one device-wide placement decision.

Administrative WAN state does not own the shared receive lifetime. DMA1 keeps
servicing LAN CPU misses while LAN is up, including when WAN is down. The
existing `rx_running` state controls NAPI enable/disable, and board teardown
closes both Ethernet devices before deleting either NAPI instance or freeing
its netdev. Rings, IRQ placement, poll budgets and forwarding policy are
unchanged; DSA's per-CPU GRO cells retain their existing native path.
Better receive service does not establish throughput fairness or
eliminate congestion elsewhere in the network stack.

## CPU-miss egress observation

`ethtool -S wan` also reports GSW-R logical CPU port 0 egress as
`cpu_port_tx_*` counters: unicast/multicast packets, six packet-size bins, good
bytes, congestion-management drops and other transmit drops. Ordinary misses
are mapped to this port and its PMAC destination feeds the existing RX DMA0
owner. The counters therefore describe switch egress toward the CPU, not output
on the physical WAN cable or proof of a completed DMA receive descriptor. They
include both physical-WAN ingress and LAN P2P misses; hardware-assisted returns
use their separate endpoint and existing PAE counters.

The native BM table mapping is port 0 to table 0 and physical WAN port 15 to
table 23. The driver enables only the GSW-R CPU-port counter-enable bit at probe
with a read-modify-write; forwarding settings and other BM port bits are
preserved. Reads use the existing BM mutex, bounded busy polling and non-clearing
read operation. Packet counters are native 32-bit values; good bytes use the
existing coherent high/low/high 64-bit read. Counter wrap and the actual sample
interval must be considered when calculating deltas.

The separate `cpu_port_rmon_read_failures`, `cpu_port_rmon_last_read_ok` and
`cpu_port_rmon_enabled` fields qualify the sample. A failed read retains the last
complete CPU-port sample; it does not erase a valid physical-port or PAE sample.
Require enabled and last-read-ok to be one and no read-failure increment at both
ends of a comparison. Mode-dependent total/broadcast counters remain omitted.
These aggregates complement physical-port RMON, host NIC counters, completed
packet captures and software RX statistics; they do not assign a drop to an
individual packet or measure DMA-ring occupancy. The getter never enables,
clears or resets counters and never reads or changes DMA status, IRQ policy,
timers, queue configuration or forwarding state.
