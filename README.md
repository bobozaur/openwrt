# AX50 work archive — 2026-10-23

This dated successor to `archive/20261020-work-index` records the reconstructed
source owners, frozen defaults, complete functional composition and native
contribution restoration inputs. The index is an archive, not a build input or
an upstream submission. `branches.json` contains every exact source revision
and historical tag mapping.

## OpenWrt owners

The selected official OpenWrt baseline is `bf763871e981e88a9d3f717d2473f6b9478984fb`;
the selected Linux version is 6.18.55. The personal default remains `main`.

| Branch | Purpose | Source tip |
| --- | --- | --- |
| `main` | Frozen official OpenWrt baseline | `bf763871e981e88a9d3f717d2473f6b9478984fb` |
| `ax50-port` | Reduced GRX500/AX50 port, source-built WAVE driver and build-time original firmware recipe | `d1cb4a855aa5b45e428c588c32b9bb3e6e0582da` |
| `ax50-debug` | Reduced port plus optional diagnostics | `82b84bfe2130df0477dac5b5309f504cd1a880ab` |
| `topic/kernel-fixes` | Shared kernel corrections | `e6ffe7ae037fb0fa7b58da929c77e4f38caae10d` |
| `topic/grx500-extras` | Optional routed-flow assistance, receive steering and boot-health policy | `b7229bcaa05ebd4da3139539340d867cacad05d0` |
| `topic/wave-extras` | Optional AP/STA, ordinary four-address/AP_VLAN, multicast and vendor controls | `498977d74c97df24ee32c2cc6617c82a2403c909` |
| `topic/wave-firmware-fixes` | Focused WDS/control-port and BlockAck/first-MPDU firmware instruction overlays | `88faaa095bc9da4f60bf78f9cf14ed82199b8be6` |
| `topic/wireless` | Generic wireless lifecycle, scan consumers, WPS enrollment, LED and steering ownership | `2bcf66523b78f6346994e564443441c9bad02134` |
| `topic/network-services` | Network service and protocol corrections | `ef5bf2af5c1ad1d845e3c80b03e02b662ea77452` |
| `topic/system-storage` | System, configuration transaction and storage corrections | `b2c7b6abed8b616eeef322659f3e51aa7af80aa7` |
| `ax50-integration` | Complete functional composition and reproducible full profile | `9fc2bcf1e0d50859e9e711dabba5c189ba870267` |
| `ax50-integration-debug` | Functional composition plus optional diagnostics | `8742084457d496027e51867ca9dc9b36cabc207a` |

Concern owners retain logical attributed commits. Core-dependent GRX500 and
WAVE owners include only their actual prerequisite closure; the firmware
correction owner extends the optional WAVE owner. The functional composition
combines the retained concerns, and debug adds optional diagnostics.

`topic/rtw88` and `wave-firmware-experiments` are historical names rather than
active source owners. Their complete histories remain archived. Generic
upstream USB support remains available, and ordinary RTW build inputs are owned
by the generic wireless branch. Custom Mesh/prplMesh and broader experimental
firmware code are excluded from the functional composition.

## Defaults, feeds and build inputs

The [packages fork](https://github.com/bobozaur/packages) and
[LuCI fork](https://github.com/bobozaur/luci) each retain `master`,
`topic/services`, `topic/vpn`, `topic/wireless`, and `ax50-integration`.
Their defaults are frozen official master revisions; their functional branches
compose only retained ordinary service/VPN/wireless contributions.

| Feed | Frozen revision |
| --- | --- |
| [packages](https://github.com/bobozaur/packages) | `55cd4e40203949b8efc214c4fb9434793b8d2a59` |
| [luci](https://github.com/bobozaur/luci) | `7d08b2c23749129bd1a20e5dc912d956c4e79926` |
| [routing](https://git.openwrt.org/feed/routing) | `4b9891b9136259f93294a424507ed24c5e8c1cbd` |
| [telephony](https://git.openwrt.org/feed/telephony) | `5d68d53c160a325ea9d03fce393e051573bcc736` |
| [video](https://github.com/openwrt/video) | `6917907d4d6c670cf85be525d23216595655061d` |

Use the functional branch's committed `feeds.conf.default`, ordinary feed
commands and `config/ax50-full.config`. Its device/profile documentation
explains the complete full image, fresh smaller embedded-initramfs selection,
normal first use and platform-owned installation/upgrade constraints. The
source does not embed per-device configuration.

The ordinary firmware package downloads pinned public inputs at build time and
verifies the original nine-file firmware cohort. Focused WDS/BlockAck overlays
remain actual firmware instruction modifications. This index includes no
firmware image, vendor source archive, cache or binary build output, and grants
no firmware modification or redistribution permission.

## Historical restoration

The OpenWrt fork preserves exact pre-reconstruction local histories under
`archive/20261023-local/heads/<branch>` and
`archive/20261023-local/tags/<old-tag>`. Its separately fetched published
histories are under `archive/20261023-remote/heads/<branch>` and
`archive/20261023-remote/tags/<old-tag>`. All 299 dated root archive refs were
published and compared with the original exact object IDs before reconstruction.

The packages and LuCI forks preserve their original local and separately
published histories under
`archive/20261023-reconstruction/{local,remote}/<branch>`. All 20 such personal
feed archive tags were published and verified before their defaults changed.
Earlier 20261020 archives and the previous work index remain unchanged.

The original independently reviewed source remains publicly reachable through
`archive/20261023-review/ax50-integration` at
`3f832df0ec6b1a5adc7ef292fc1510411bb0e57d` and
`archive/20261023-review/ax50-integration-debug` at
`7d693c8e2e566f4590ea5c6e5c40177abb5b56e8`. The initial reconstructed root/feed source
cohort remains preserved by create-only `candidate/20261023/ax50-integration`
tags, plus root `candidate/20261023/ax50-integration-debug`.

The subsequent scan-corrected root source is identified by
`candidate/20261023/ax50-integration-scan-fixes` at
`e2c6ac63a973aadd24eac08dd71350d77dcabe96` and
`candidate/20261023/ax50-integration-debug-scan-fixes` at
`012629a1bc86a7ff2d0933d6f6d6145f427da14c`. It preserves the earlier source
cohort and corrects the first native scan watchdog budget plus ucode scan
event/interface/deadline/request-error ownership. Packages and LuCI pins are
unchanged. Candidate source tags preserve exact inputs independently of the
established working heads. The initial packages review commit is already an
ancestor of its corrected source candidate and needs no extra reachability tag.

Fetch the desired tag from its recorded personal repository, inspect its object
ID against `branches.json`, then create a local restoration branch from that
tag. Local and published histories can differ; choose the recorded namespace
for the history being restored. The index records source identities without
requiring the corresponding working branch to have been replaced.

The later native MAC ownership correction is identified by
`candidate/20261023/ax50-integration-mac-fixes` at
`b7c3502d1fd56f9a20e2a4a52cbe051d5e4f40fb` and
`candidate/20261023/ax50-integration-debug-mac-fixes` at
`cc3214e24aac36ecb6bdbdeafdaa32d9085248a1`. Deferred supplicant
configuration retains reservations only for real native interfaces and
acknowledges retirement before setup succeeds. AP reservation-error cleanup
also uses its correctly bound native teardown owner. Earlier candidate tags,
feed inputs, kernel, WAVE and firmware sources remain unchanged.

The following scan-consumer cohort is identified by root
`candidate/20261023/ax50-integration-scan-consumer-fixes` at
`9fc2bcf1e0d50859e9e711dabba5c189ba870267` and
`candidate/20261023/ax50-integration-debug-scan-consumer-fixes` at
`8742084457d496027e51867ca9dc9b36cabc207a`, together with the LuCI tag
`candidate/20261023/ax50-integration-scan-consumer-fixes` at
`7d08b2c23749129bd1a20e5dc912d956c4e79926`. Numeric interface identity is
preserved by the native ucode codec, and the executable CLI reports scan
failure. Privileged C scans use active wildcard nl80211 requests and kernel
completion/abort status; rpcd and LuCI retain failures rather than converting
them to successful empty results. Package floors keep these consumers paired.
Ordinary multicast identifies the interface, not a unique concurrent scan
invocation. The finite scan-event budget does not impose an end-to-end bound
on unchanged metadata, acknowledgement or result-dump transport.

## Native owning repositories

`native/` contains the twelve preserved prerequisite Git bundles and metadata
from the separate native owning repositories. They contain source commits;
public upstream history is excluded. Each JSON lists its official source URL,
required public base, retained branch, exact refs, bundle checksum and verified
bundle state.

| Native owner | Public base | Retained source tip |
| --- | --- | --- |
| firewall4 | `c2ae8c8940a89407da32fbd662d4010ee2c9bbe6` | `6c12d171f4240d355692a182b2e400774874ed67` |
| fstools | `907cf4496eb58c93daf3c9c65ce32eec6e6546e3` | `65266fa0ff775798488c0d439a8fc5b91d8fe098` |
| linux | `725bd2f3c81d54edccb66fead01d4c0c222e2231` | `01dbe7ddc4bd3a95fbd3d74ba1428dce64cda84b` |
| netifd | `06fc904585606ed5a1dfca233ecaa6be9a5ca599` | `c40962818ffa19016747ebcea02e207285942a03` |
| nftables | `824778066c2f84e7207cba7d1048a0def8bd98fd` | `71a1a4e583f9a9fdc47eaaacb77a494299b0408e` |
| odhcpd | `68f382690bfaec56d5b1f31c3c31c48bcb642e3a` | `ef6db0fe032e076cc3466e22531bc95d12c781e3` |
| omcproxy | `3abb601ab77f5e914a3203e5d0c8ae7fb1579bae` | `c9036a5eb5a15f063953981facd21884c78e0194` |
| ppp | `2aef1a81dbe093d8de75396296ea233d2f42ae8a` | `9b5fd243726fc612f223ee7b3d112011c504193a` |
| procd | `a9afc12dcd4e1f86ef08eb6744b9a6359bd78ed8` | `234e7fa06c19c705d49f99e727cd1941bd4a58aa` |
| rpcd | `d99f703e4035d03fbcfbada3cb697db346c2b984` | `8e97b32837748ad7b4a7bc3f52fc22d2bb8e4e8c` |
| ubus | `414c60a2e29e72c2c4f573bc50ad54aef076d169` | `f52ec31c08e2788d9277b2500bc55969e46db45f` |
| uci | `74f6277aabffc943d026f406df57c22595134c42` | `6de6c3d6bf60daac4c3816e0c78e7e05478a585c` |

To restore one native component:

1. Clone its official repository and fetch the exact public `base` recorded in
   the component JSON, including the selected Linux tag where applicable.
2. Check the bundle SHA256 against that JSON and run
   `git bundle verify /path/to/component.bundle` inside the clone.
3. Fetch contribution branch refs with, for example,
   `git fetch /path/to/component.bundle 'refs/heads/*:refs/heads/restored/*'`.
4. Fetch historical `refs/tags/archive/*` refs similarly, or select a recorded
   object already present in the public base.

`public_base_refs` identifies public refs intentionally omitted from a bundle.
Linux follows selected 6.18.55 and excludes the optional USB add/retire pair.
rpcd retains ordinary WPS targeted committed-wireless reload, propagates native
scan failures and excludes the Mesh batch API. PPP uses upstream's MPPE
option-mask correction and retains its independent MTU/CCP lifetime fixes. Older contribution versions remain archived.

These native histories are restoration and contribution sources, not custom
publication destinations. Their current official bases may differ from the
source pins selected by OpenWrt packages; the package recipes and patch owners
remain authoritative for an actual OpenWrt build. No official upstream received
a push as part of this reconstruction.

## Qualification boundary

The branch map and bundles preserve source and restoration identity. They do
not establish a successful complete production/debug compile, image fit,
installation, hardware acceptance or performance measurement. Source preparation
and checks must be interpreted within their recorded scope; native API,
package closure and device behavior require their ordinary build/runtime gates.
