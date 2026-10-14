# Linux bridge and four-address forwarding

The native software datapath uses the Linux bridge as the multicast policy
owner on bridged AP interfaces. The bridge owns VLAN identity, IGMP/MLD
membership, INCLUDE/EXCLUDE source lists, querier state and expiry. Its
multicast-to-unicast copies already carry the receiver's Ethernet address;
WAVE resolves that receiver to the station SID without creating another
source-filter table.

The accompanying bridge patch includes the reporting host in the `(port,
VLAN, source, group)` lookup and applies INCLUDE/BLOCKED filtering before
multicast-to-unicast conversion. Changing multicast-to-unicast mode discards
dynamic membership because a port aggregate and a host-specific report have
different owners. Reports relearn membership through the existing bridge
querier/timer machinery. Permanent entries remain owned by their administrator. On conversion ports,
user MDB add/replace/delete selects the port-wide wildcard entry. An eligible
wildcard or router-port delivery emits one unchanged group frame and suppresses
duplicate host copies; a blocked wildcard does not hide an eligible host.

Every bridged AP receive frame enters the Linux bridge before intra-BSS
forwarding. Netifd's WAVE-specific runtime `wireless_bridge_forwarding` property
requests hairpin for a non-isolated AP without changing multicast-to-unicast.
The actual bridge hairpin, VLAN, snooping, querier and isolation settings remain
authoritative. In particular, an explicit hairpin-off or snooping-off choice is
not bypassed or intersected with a second driver policy.

When Linux returns a unicast frame to its ingress bridge port, WAVE follows
the selected Ethernet destination even if both hosts resolve to the same
four-address station. This includes multicast-to-unicast copies and software
AP_VLAN ports. The destination station and child binding checks still apply;
the driver does not repeat bridge hairpin, VLAN or isolation decisions. Native
internal forwarding retains its source-station split horizon. Group-destination
frames and unknown-destination ARP probes retain their source-peer exclusion.

Bridge/OS transmit frames that retain a group destination use the existing BSS
broadcast group. Established four-address peers receive separate SID-selected
copies with the original Ethernet addresses. Parent replication excludes peers
bound to software child interfaces. Ordinary peers retain the three-address BSS
group path when the bridge does not convert a frame. This preserves an explicit
`multicast_to_unicast=0` setting, including its aggregate delivery semantics.
Per-host source separation on a shared AP bridge port requires the bridge's
multicast-to-unicast path and IGMPv3/MLDv2 snooping configuration.

## Receive buffer handoff

Firmware data buffers retain their native allocation, DMA base, mapped size
and descriptor-offset interpretation. After DMA unmap and bounds validation,
an ordinary native RX frame that needs IP-header alignment uses the existing
replacement allocation for a CPU-only skb with `NET_IP_ALIGN` headroom. A
non-overlapping copy supplies the consumer; the original skb is reset to its
empty DMA view and reposted through the existing checked map and ring owner.
This uses one successful allocation per delivered frame, as before, without
changing firmware buffer addresses to impose Linux alignment.

The CPU skb receives normal private-buffer initialization and carries the same
separate station, VAP and priority metadata. A consumer rejection frees only
that copy; the original remains the ring's buffer. DMA-map failure still frees
the ring buffer and queues the slot for native recovery. If CPU allocation
fails, the existing in-place alignment and post-delivery refill remain the
fallback. Already-aligned frames avoid the copy. EAPOL, firmware logging and
DirectConnect buffer ownership retain their separate paths.

Firmware buffer-chain reassembly allocates its CPU-only destination with IP
alignment before the existing fragment copies, avoiding a subsequent full-frame
move. This is host-descriptor SOP/EOP reassembly, not IP fragmentation. The
change preserves fragment retirement and failed-allocation cleanup. It does
not remove the ordinary RX copy or claim a measured throughput improvement.

## Standalone native multicast

An active AP outside a Linux bridge owns its native IGMP/MLD reports. The report
key contains the station incarnation, reporting Ethernet host, normalized VLAN,
protocol and group. Hosts behind one four-address peer keep separate source
lists; delivery to that peer is their union. All six IGMPv3/MLDv2 record types,
legacy ASM join/leave, INCLUDE/EXCLUDE transitions, running and blocked source
timers, general/group/source queries and membership expiry use one host policy
engine under the existing multicast lock.

The VLAN key admits at most two physical C/S tags, including an skb metadata tag.
Tag order, each nonzero VID and its 802.1Q/802.1ad type identify the domain. PCP
and DEI are ignored. VID 0 supplies priority only: a lone priority tag shares the
untagged domain, and a priority tag around a nonzero tag does not create another
domain. The physical two-tag bound is checked before normalization. Packet bytes
and metadata are preserved; generated queries use the normalized tags with
priority zero. More deeply stacked frames are withheld from native multicast.

Reports and queries undergo bounded IP/header/tag checks, payload checksum
validation and complete record/source/auxiliary-length validation before any
record changes policy. Control traffic, IPv4 link-local groups, IPv6 link-local
scope, IEEE 1905.1 and explicitly configured flood ranges retain their flood
policy. Other non-IP Ethernet broadcast traffic keeps its existing path.

Reliable multicast selects an immutable receiver class for the packet's VLAN,
group and permitted ordinary station set. A class holds exact station/SID
incarnations and an exclusive ID from the existing shared firmware pool. No
packet receives that ID until every JOIN has completed, every membership is
present and the pool permits a TX pin. A changed recipient set uses another
class; no later JOIN can add a receiver to an existing class. Retirement first
removes the class from lookup, withdraws confirmed memberships and releases its
host owner. Existing command, membership, quarantine and DAT_CFM TX pins still
control reuse. A stale SID, failed allocation, full pool or incomplete JOIN never
falls back to the broadcast group. Unused classes retire after 30 seconds.

Four-address peers use the established SID-selected copy path, filtered by the
same source policy. An explicit reliable-multicast-off setting retains a single
ordinary BSS group frame when the policy's ordinary-receiver union is nonempty.
That aggregate air frame does not provide per-station source isolation; the
driver does not invent ordinary-peer SID/group-DA semantics. Mode publication
requires an acknowledged SET or typed GET; an unknown mode withholds native
filtered data until authoritative state is available.

The policy limits each AP to 128 protocol/VLAN domains, 128 groups, 512 reporting
hosts and 4096 source entries. The firmware pool has 31 usable group IDs shared
with other VAPs and existing owners. Record replacement is atomic; allocation or
capacity failure withdraws only that reporter's old contribution, preserving
other reporters. Queries use at most 64 source addresses per packet and four
emission attempts per master tick. General and last-member work share the budget
fairly. A failed query admission retains the retry and does not start shortening
its report-triggered last-member timer.

The existing radio master owns the timer command and a bounded query send list;
there is no separate worker or policy daemon. Query packets reserve TX capacity
before emission is accepted, then transmit outside the policy lock. Untagged
IPv4/IPv6 domains start at AP activation. Valid observed tagged multicast traffic
activates its domain; silent, unobserved VLANs are not invented. A valid external
querier suppresses native queries in that domain; learned timing and address
election remain per VLAN/protocol. Native IPv6 queries require a usable
link-local source and remain subordinate to an observed external querier.

A netdevice upper-change notification resets the native policy and retires its
classes when bridge ownership changes. The master settles the event through a
nonblocking RTNL acquisition, covering bridge unlink ordering and rejected
attach attempts. Removing the AP from a bridge restarts standalone discovery
without waiting for multicast traffic. Each queued query rechecks the current
owner and policy epoch before TX. AP close cancels the timer command and existing
membership commands before releasing their policy, station and group owners.

## Software AP_VLAN children

Software child packet counters use the Linux netdevice per-CPU statistics
owner. RX counts valid Ethernet frames at the child handoff to the network
stack; TX counts frames accepted by the native transmit path. Child rejection
and submission failures count as drops, while a busy hardware queue leaves the
packet with Linux without incrementing either counter. Hardware-submitted skbs
still refer to the parent, so counter updates do not extend child lifetime into
firmware completion. These are Linux port counters, not over-air ACK or retry
measurements.

Parent interface stop closes its AP_VLAN dependents under RTNL before the
cfg80211 deletion callback takes the wiphy mutex, matching mac80211 ordering.
Close notifications can then take that mutex without recursion. Ordinary stop
does not unregister the child objects. Existing BSS/station owners still retire
bindings, and deletion keeps the established multicast/netdevice retirement
ordering.

A four-address AP_VLAN is a software Ethernet interface created with
`use_4addr=1`. It initially has no BSS or station owner. The normal hostapd flow
creates the child, assigns the intended BSS MAC while DOWN, and opens it before
binding the station. An explicit creation MAC is also supported. A default
address copied from an existing AP is only an initial address, not a parent
selection. First open requires exactly one active AP on the same wiphy with
the child's final MAC. Inactive master/control aliases do not qualify.

Successful open pins that parent for the child's lifetime. Ordinary close and
reopen preserve the attachment; changing its MAC after attachment is rejected.
Headroom and tailroom are reserved before registration, while open checks the
selected parent's requirements without changing the administrator's MTU or an
upper device's reservation. Empty children can be deleted normally and are
removed before wiphy teardown. Their station queries and parent-dependent
vendor controls return normal errors rather than dereferencing a missing BSS.

A child allocates no firmware VAP. The normal cfg80211 station and key owner
remains the parent BSS. Children expose no separate management-frame owner;
their registration cleanup cannot change the parent's receive filter. EAPOL
transmission remains on the parent control path, and an EAPOL frame submitted
through a child is rejected. Ordinary AP creation still applies a requested
MAC through the serialized VAP setter before registering the netdevice.

`NL80211_ATTR_STA_VLAN` binds one already-four-address station to a child. Binding
rejects another BSS, a busy child, a removed station incarnation and an ordinary
three-address peer. A station can move between children or return to its parent.
The radio master withdraws the exact station incarnation's native multicast
commands and confirmed memberships before publishing a child binding. Native
membership admission stays closed while the child owns that station. Returning
to a live, accepting parent reopens admission. Standalone native memberships
relearn through reports and queries; donor profiles retain their existing
default-membership restoration. Failed or uncertain transitions do
not reopen admission. Station removal clears both sides of the binding; parent
deletion withdraws all children before releasing the parent driver state.

Receive selection uses the firmware-provided SID before host learning and
intra-BSS forwarding. Child unicast, unknown-destination and group transmit all
select the bound station. Parent destination lookup, including old host-table
entries, cannot bypass that child. A station reference covers native submission,
and the skb refers to the parent before hardware takes ownership. Child deletion
withdraws the binding, waits for receive readers, and unregisters the Linux
netdevice so its queues and receive backlog are drained. Previously submitted
packets retain their original parent ownership; a binding change does not claim
to retract those hardware-owned frames. Queued Linux-port
traffic chooses the current binding when dequeued; final submission rechecks it.

Bridge membership, VLAN filtering, STP and port isolation belong to the Linux
configuration owner. Parent AP `isolate`/hairpin settings do not automatically
establish isolation between distinct child bridge ports. Use the normal bridge
port policy for those children; this driver does not infer such policy from
its AP-forwarding flag.

Hostapd supplies its configured local Multi-AP role in the Association Response
IE template during AP setup, through standard `NL80211_ATTR_IE_ASSOC_RESP`.
The accepted template's backhaul-BSS bit and a separately parsed peer
backhaul-station declaration select the existing four-address station type
before firmware station creation. The target BSS owns that local role; it is
independent of the master serializer interface. Failed or omitted template
updates preserve it, an explicit replacement can clear it, and BSS teardown
clears it. A peer IE alone cannot enable the mode on an ordinary BSS.

The bounded parser selects the Multi-AP type within the complete IE list and
rejects duplicate or malformed extension declarations. A WFA OUI alone or a
fronthaul-only role is insufficient. AP setup enables hostapd's existing
association-time WDS child creation for configured backhaul BSS values 1/3;
plain and fronthaul-only late WDS remain unsupported. The normal Association
Response packet retains its existing producer; no beacon IE or private control
operation is fabricated.

Ordinary protected, shared dynamic-VLAN AP_VLAN support still lacks a firmware
key-context contract. Hostapd's `src/ap/wpa_auth_glue.c:609` selects the VLAN
interface for an independent GTK. WAVE's `UMI_SET_KEY` in
`shared_mbss_mac/mhi_umi.h:1438` has station ID, key type/index, cipher, PN and key
bytes, but no child/VLAN GTK domain. `_mtlk_core_set_enc_ext_cfg` addresses group
keys through the parent firmware VAP and broadcast SID. The TX descriptor's
SID/MCIDX, VAPID and MCF fields likewise contain no child group-key selector.
Using normal GTK indices would overwrite the parent's or another child's key.
Static four-address children use their peer's parent-owned pairwise key and do
not establish a shared ordinary-child group-key context. No software encryption
bypass or new firmware operation is assumed.

Four-address mode and peer-list changes configure the next association. They
share the radio-master serializer with station creation, keys and retirement.
A different global mode returns `EBUSY` while the BSS has peers; a changed list
entry returns `EBUSY` while that peer exists. Unchanged requests remain harmless
no-ops. Failed or uncertain native retirement cannot authorize a policy change.
The former unacknowledged vendor flush notification no longer stands in for
station removal.

A trusted controller can disconnect its peer through the ordinary hostapd and
station owners, wait for native removal, update the configured mode/list, and
reassociate with a fresh key lifetime. Native retirement completes traffic
stop, station removal and SID release before removing the host station and its
child binding. Association applies the current policy under that same owner;
queued peer declarations cannot preserve a superseded mode decision. This path
does not migrate a PTK or restore its packet number, and requires its own late
transition runtime qualification. Automatic WDS discovery from an unknown data
frame remains unsupported. Native STA wrong-transmitter-address behavior is a
separate unresolved defect. Optional donor DirectConnect/UDMA paths are outside
this software forwarding owner.

## Integrated validation

These changes require the integrated kernel/driver build and physical campaign;
source review does not establish hardware behavior. Coverage includes two hosts
sharing `(S,G,VLAN)`, conflicting INCLUDE/EXCLUDE lists, all report transitions,
query retry/expiry, stacked and priority tags, conversion/snooping/hairpin mode
changes, static MDB interaction, bridge attach/detach, reliable multicast off,
source-policy changes with outstanding TX, pool pressure and station/SID reuse.
Child coverage includes local/peer Multi-AP role admission, wrong-parent/busy
binding, reassignment, station removal, child/parent deletion under traffic,
bridge VLAN/STP/isolation, unknown/group TX, and mixed ordinary/four-address peers
without duplicate parent delivery. Trusted late transitions require native
retirement, fresh association/keys and data qualification. Automatic late WDS,
shared protected ordinary AP_VLAN and the native STA transmitter-address defect
retain the separate limits above.
