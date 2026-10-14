# Native radio contracts

The station wiphy uses mac80211's single-channel context emulation, including
the native add, remove, change and switch adapters. mac80211 owns replacement
reservations and waits for every interface sharing a replaced context to be
ready. The advertised one-channel limit is unchanged. Registering the switch
adapter allows that reservation path; it does not add independent hardware
channels or bypass the paired AP's channel and DFS permission checks.

For an accepted upstream CSA, WAVE's existing `channel_switch` callback submits
the firmware command and reports its blocked confirmation through
`ieee80211_chswitch_done()`. mac80211 then commits the reserved context through
the standard switch adapter and WAVE's existing `.config` path. The adapter
also handles native VIF reassignment and uses the same shared hardware channel;
there is no separate firmware programmer. The upstream emulation helper does
not propagate a `.config` error in its return value, so a successful context
operation alone is not firmware or over-the-air completion evidence. Current
channel state and actual payload remain necessary to qualify a completed CSA.

Ordinary station configuration uses a normal channel command, not a timed CSA.
This includes station activation and the emulated context commit after an
acknowledged switch. The existing shared-radio serializer still rejects an
ordinary request that differs from the active AP home channel. Only the actual
`channel_switch` owner requests the upstream countdown; scan excursions and
returns keep their separate scan/normal commands. AP presence alone must not
manufacture a same-channel CSA or restart a completed countdown.

An initial AP coexistence scan after CAC cancellation can encounter RF-off
with the radar transmit fence still active. With no active AP, station, peer
or CAC owner, the scan first completes the existing normal channel command to
a legally beaconable 20 MHz channel from its requested list. Native regulatory,
generation and firmware-confirmation checks retain ownership of the fence;
the scan never enables RF directly or clears DFS state. The confirmed channel
also becomes the scan return target, including cleanup after later failure.
The subsequent scan and final AP width selection remain native. If no requested
channel is legal or the command fails, setup retains its ordinary error path.

Native Gen6 master initialization sends the existing radio-wide HE MU scheduler
setting through `UM_MAN_HE_MU_OPERATION_CONFIG_REQ` (`0x047a`). The four-byte
`UMI_HE_MU_OPERATION_CONFIG` carries the enable value, SET operation and reserved
bytes. The TXMM allocator clears the payload. Initialization waits for the normal
command confirmation and propagates failure before a BSS is published. The
existing radio PDB owns the policy, including its default of enabled; recovery
already reapplies that policy. Per-BSS HE capability toggles do not independently
change this shared radio setting.

HE-MU queries use the native requested-member control protocol and report the
configured radio value. Gen6 A0 legacy `MU_OPERATION` SET and GET return
unsupported because the firmware acknowledges that command without applying
its payload; stale legacy requests are skipped during recovery. This leaves
the independent HE-MU control available. TX-power reporting keeps the
conservative VHT-MU limit family eligible when legacy MU control is unavailable,
instead of treating its ineffective cache as an enabled or disabled hardware
state. The report remains a calibrated, configured power envelope rather than
measured RF output.

The vendor `FixedPower` command reaches ordinary peer power calculation and
TCR updates despite its host-side management-power label. Its legacy request
remains separate from the standard fixed DATA policy. With the matching patched
A0 firmware and supported native power tables, standard FIXED requests accept
600 through 3150 mBm in 50 mBm increments, subject to the current conservative
power ceiling. Unsupported values and requests that would initially clip are
rejected before success is reported.

The firmware keeps pending and applied policy separate. The normal channel
transaction commits calibrated DATA power across peer, VAP and paired downlink
group owners; a read-only query reports the applied value. Integer dBm readback
rounds down half-dBm values. Peer and channel changes retain the requested policy
while respecting current legal limits. Firmware initialization clears the
policy, and recovery reapplies the last accepted request. Automatic and
management calculations retain their native behavior. Native FIXED policy
application, readback and lifecycle are qualified within the supported power
tables; this does not turn the configured value into a measured RF output.

On Gen6 A0, an active automatic or limited-power change uses the existing
normal channel transaction on the unchanged channel. `SET_TPC_CONFIG` alone
only stages its table; success waits for the native channel phase to refresh
the calibrated limits and peer DATA state. The new user/MAC policy is cached
only after that checked completion. A same-channel refresh consumes the full
new table so an increased limit cannot retain the previous lower ceiling;
ordinary channel changes keep their existing table intersection.

Scan, CSA, CAC, DFS and wait-for-beacon fences retain their current owners.
An active power change encountering those fences returns busy without
publishing a new user/MAC policy. If a later transaction step fails after
staging a table, the existing quiesce/recovery path prevents an unapplied
table from being exposed as the operating policy. Recovery rebuilds limits
from the last accepted request. These are completion and configuration
guarantees; measured RF output is a separate property.

Gen6 A0 rejects FixedPower selectors outside the firmware's 0 through 4 domain
before storing or submitting the request. A query with no stored tuple returns
unsupported, so the fixed-size vendor response cannot expose unwritten values.
For a stored tuple, the four returned values describe the existing requested
configuration; they do not prove firmware application or measured RF output.

A scheduler command confirmation establishes configuration, not exercised OFDMA
traffic. AP MU-EDCA advertisement, firmware group scheduling and STA MU-EDCA
contention timers have separate owners. The native station queue callback still
translates ordinary EDCA only. `UMI_AC_WMM_PARAMS` has contention windows, TXOP,
AIFSN and ACM, but no MU-EDCA timer or alternate contention set. The retained
interface does not establish whether station firmware autonomously applies
received MU-EDCA elements. No setter or timer encoding is inferred from the HE
scheduler command.

The standard AP station airtime-weight callback uses the existing A0 dynamic
weighted ATF transaction. It is advertised only when the actual loaded firmware
contains the complete non-mutating individual-SID TXOP read correction. The
radio master serializer resolves the MAC in the originating VAP, pins its peer,
and enumerates every current AP peer on that radio. It uses their native SIDs,
not AID-minus-one, and includes connected idle peers; firmware owns queue
eligibility and service of unused grants. STA-role peer membership and temporary
unconnected-station measurements cannot bypass this AP policy owner.

Weights are positive 16-bit values, with an implicit default of256. If `M` is
the largest current weight, the native per-AC quantum is
`floor(floor(32767 * weight / M) / 128)`. The driver preserves this less-than-one
quantum floor rounding and rejects an update or new peer whose quantum would
be below20, where the firmware would otherwise clamp the requested ratio.
All four ACs receive the station's same weight. This is the native relative
weight API, not a conversion to percentages of connected-station counts.

Before an update, native HE-MU GET must show that its global group owner is
disabled and drained; this API does not silently change that policy. Non-HE VHT
MU beamformees are unsupported because their independent native GroupManager can
override the saved TXOP policy. A card-wide sleeping lock excludes HE-MU enable,
explicit static plans and numeric-SID TXOP writes while any radio owns standard
weights. Eligible peers' corrected native GETs must then show a supported
non-disabled saved policy for every SID. The driver does not force a mode or
infer a new peer's policy from a reused SID. Firmware still checks its current
membership and effective mode. A failed acknowledged update restores
the previous complete vector before returning the error; an uncertain write or
failed rollback fences the hardware. Host weights and station-info reporting
publish only a confirmed complete policy. Unchanged weights do not reset credit.

ADD admits the new vector before publishing the peer to the host data path and
uses the existing native remove/SID owner on failure. REMOVE confirms ATF disable,
retires the native peer, and rebuilds the remaining vector before returning.
An unrepresentable or unsupported new membership is rejected without silently
changing existing weights. Last-peer removal withdraws the standard owner.
Channel setup withdraws and re-applies the complete checked vector before
publishing success; an uncertain channel/policy result fences the hardware.
Recovery retires all old peer/weight incarnations; ordinary hostapd association
handling supplies fresh policy after its station flush. Vendor quota and TXOP
writes cannot replace a live standard owner. A live vendor quota owner must
first acknowledge its normal disable operation before standard weights apply.

The vendor quota command keeps its bounded fixed header/SID array and native
one-byte result. The existing hostapd airtime error/cache rollback correction
is needed to expose a rejected standard update truthfully to its caller.
Proportional reported airtime, idle/resume service and multi-user behavior still
require native qualification. `AirtimeUsage` combines separately polled RX/TX
values and is not a backlog predicate; the same-incarnation native SU TX-time
counter supplies the bounded measurement for that qualification. No scheduler
eligibility rule is synthesized from throughput or host queue estimates.

The remaining capability boundaries are deliberate:

| Feature | Retained contract and unresolved requirement |
| --- | --- |
| DCM | Unadvertised and excluded from peer negotiation. Local beacons requesting DCM are rejected; the selected firmware contract has no qualified directional profile or rate-selection behavior for this feature. |
| ER | Unadvertised; local beacons requesting ER are rejected. The selected firmware peer contract receives only the extension ID, six MAC bytes and PHY0–7, without a qualified ER tail interface. |
| Final-to-draft tail | Final DCM maximum-RU bits occupy PHY8 bits 6–7; the retained draft uses PHY9 bits 1–2. Copying modern trailing PHY bytes verbatim is invalid. An 18-byte UMI array does not prove the currently uncopied tail is usable. |
| TWT | Unadvertised and rejected by the AP beacon contract. The selected firmware has no usable native agreement/responder activation interface. The incomplete host extension is excluded from the default patch set; retained private work is not a supported feature. The 72-byte `UMI_SET_BSS` has no TWT member, and its reserved bytes remain unused. |
| 80+80 | The retained host channel producer excludes the second-segment implementation, HE MCS records carry only two width pairs, and matching firmware diagnostics reject 80+80 channel/CSA handling. Enabling an enum or capability bit cannot supply the missing firmware path. |
| Mesh and IBSS | Existing ordinary VAP creation selects AP or infrastructure STA. Multi-AP roles and an IBSS scan classification do not provide mesh/IBSS beacon, peer or key lifecycles. |
| Firmware monitor | With the matching patched A0 firmware, the first full-MAC wiphy exposes an exclusive, receive-only cfg80211 monitor interface. Both native radios must have no active AP, station, scan or CAC owner. Native channel change, stop, same-interface reopen and fresh frame reception are qualified. Injection remains unqualified and disabled. |

The monitor receive path retains complete raw 802.11 frames and FCS bytes and
publishes only qualified radiotap fields. Received protected bytes are preserved;
the driver performs no decryption. Stop and retune use complete card firmware
recovery and preserve inactive native VAP and configuration ownership. Concurrent
native roles and active monitor acknowledgments are unsupported.

The monitor netdev consumes outgoing packets without submitting them to firmware
and increments its drop/error counters once. The typed monitor TX entrypoint
returns unsupported without taking packet ownership; standard and vendor
management requests on the monitor are also unsupported. A successful packet
socket write only establishes that the netdev consumed the packet. Earlier
native completion/accounting results did not establish monitor over-air
injection, so they do not enable a transmit feature. Ordinary AP management,
EAPOL and native DATA paths retain their existing behavior.

HE STBC is neither advertised nor negotiated in either direction. Explicit
native STBC modes 2 and 3 return unsupported before firmware submission or
configuration publication: the forced two-chain mode produced a PHY watchdog,
and the four-chain mode is not qualified for this board. AP SU beamforming,
ordinary HE rates and widths, original HT/VHT receive capability fields and
observed radiotap STBC/DCM metadata retain their separate contracts. Unsupported
local HE beacon bits are rejected rather than accepted with a silent downgrade.
These boundaries describe the selected driver/firmware interface; they do not
establish general silicon limitations.

Native AP and STA byte RTS thresholds use the same radio policy. Support is
gated by the complete loaded firmware extension, including its reserved storage,
startup initialization, constructors and protection writers. Older firmware
rejects enabled thresholds with `EOPNOTSUPP`. Values from 0 through 2353 are
accepted; the standard disabled value withdraws the byte policy. The STA Ethernet
datapath bypasses mac80211's software RTS decision, so enforcement resides in
firmware.

The extension selects native size comparison and updates both RTS/CTS length
fields. Native forced-protection methods remain authoritative. Per-peer and
group shadows retain the latest native methods while the policy is active;
withdrawal restores those methods rather than stale pre-activation state.
Changes are staged and applied within the native channel transaction while TX
is fenced. A terminal query must confirm the applied tuple before the driver
publishes it. Channel setup and recovery replay acknowledged policy, and peer
constructors inherit the active threshold. A failed or ambiguous transaction
does not publish an unconfirmed host value.

AP requests combining RTS and long-retry changes are rejected before mutation;
the STA path validates retry candidates before submitting RTS. The companion
cfg80211 single-radio threshold fix preserves normal parameter publication.
Both native AP bands and the native STA path have frame-level byte-policy
qualification, with separate HE DATA and activation/recreation coverage. The
hardware comparison uses PPDU/PSDU size; one MPDU alone does not establish an
aggregate's comparator input. Fragmentation remains unsupported.

The RTS bandwidth-signalling query returns only the last host configuration
acknowledged by firmware. It returns unsupported until such a pair is known.
The matching A0 handler does not implement its declared GET operation: it
applies the two flag bytes even for a query, so a zeroed request would disable
signalling. The driver therefore reads its existing radio configuration
without sending this command. The setter still publishes only after its
confirmation, and recovery replays the saved configuration. This cached value
is not independent firmware-state telemetry and does not supply a byte-length
RTS threshold.

The original A0 TXOP GET mutates its upper-MAC mode table. Individual-SID native
queries are therefore admitted only by the complete loaded firmware correction;
they preserve the saved policy under a native group override. The public query
selects the last acknowledged numeric SID, not a new MAC/peer incarnation.
ALL and older firmware remain configured-only, and unknown tuples return
`EOPNOTSUPP`. The standard airtime owner instead selects each current SID under
the shared peer serializer and checks the native result without changing it.

Aggregation GET remains configured-only because its native command is a policy
setter and does not populate the window reply. Its per-core AMSDU/BA/window
tuple retains zero's "no change" meaning. Neither query supplies a negotiated
BA window, byte-length RTS policy or fragmentation implementation.

Association requests supply bounded per-peer radio/vendor hints to the native
userspace MLME path. Reading these hints does not consume them or extend their
30 second lifetime: hostapd may remove and re-add a station while processing
queued association retries. A newer received request replaces the snapshot and
BSS stop/start or flush clears it. Current nl80211 parameters own the AID,
negotiated capabilities, MFP and authorization; the current local backhaul role
independently gates Multi-AP admission. The hint cache is not a station/key owner
and cannot identify an exact association request across the netlink boundary.

Native STA group-key revocation still requires acknowledged firmware VAP
removal. Once the old Linux association and peer have both retired, successful
VAP removal does not queue another terminal stop that would block direct
reassociation in the same wiphy transaction. The VAP remains stopped and
uninitialized until the next native BSSID notification completes activation and
BSS setup. Existing pending stops, failed revocations and live-peer retirement
keep their terminal-stop and recovery barriers.

The Debug/General interrupt-delay maxima use monotonic timestamps from the
matching IRQ domain and nonempty data work. IRQ timestamps and maxima use kernel
atomic64 access so hardirq, tasklet and control readers have coherent snapshots
on 32-bit hosts. Legacy and MSI paths both supply timestamps; empty opportunistic
ring visits cannot turn a missing timestamp into an epoch-sized maximum. These
are historical service-delay maxima shared by the card, not per-packet RTT,
firmware acknowledgement or over-air completion measurements.

The recovery startup check probes the vendor userspace FAPI netlink group. A
failed Check Socket request removes only that optional recovery-channel context;
its initialization is deliberately nonfatal to ordinary driver startup. The
matching warning is not a firmware-fatal notification. It also does not mean
that recovery is available: native configuration retains automatic firmware
recovery disabled. Integrating the legacy restart flow with shared AP/STA owners,
queued commands and data/key lifetimes remains separate from a healthy module
unload/reload. The generic netlink failure returned by the driver does not retain
the underlying transport errno, so this warning alone cannot identify a specific
userspace installation or socket failure.
