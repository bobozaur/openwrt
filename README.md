# AX50 work archive — 2026-10-20

This tag preserves the work behind the branch consolidation. It is an archive,
not part of an OpenWrt build or a proposed upstream patch.

The active OpenWrt branch set is:

| Branch | Purpose |
| --- | --- |
| `main` | Synchronized official OpenWrt baseline |
| `ax50-port` | Reduced GRX500/AX50 port, source-built WAVE driver and unchanged stock-firmware importer |
| `ax50-debug` | Port plus diagnostic instrumentation |
| `topic/kernel-fixes` | Shared kernel corrections, with separate logical commits |
| `topic/grx500-extras` | Optional routed-flow assistance, receive steering and boot-health policy |
| `topic/wave-extras` | Optional WAVE roles, four-address, multicast and vendor controls |
| `topic/wireless` | Generic wireless, daemon integration, LED and steering work |
| `topic/rtw88` | Independent USB Wi-Fi driver work |
| `topic/network-services` | Network service and protocol changes |
| `topic/system-storage` | System services, configuration transactions and storage changes |
| `ax50-integration` | Combined functional branches for further qualification |
| `ax50-integration-debug` | Combined functional branches plus diagnostics |
| `wave-firmware-experiments` | Dormant firmware experiment source, excluded from active builds |

The [packages fork](https://github.com/bobozaur/packages) and
[LuCI fork](https://github.com/bobozaur/luci) each retain `master`,
`topic/services`, `topic/vpn`, `topic/wireless`, and `ax50-integration`.
Logical patch commits remain separate inside the grouped branches. Submission
branches can be cut from those commits when a particular contribution is ready.

## Historical branches

In the OpenWrt and feed forks, every former local branch tip is preserved by an
`archive/20261020-consolidation/<old-branch-name>` tag in its corresponding fork.
Former remote tips, which can differ from local tips, are preserved by
`archive/20261020-remote/<old-branch-name>` tags. Fetch a tag and create a branch
from it to recover that exact history.

`branches.json` records the retained branches and historical tag mappings.
Historical firmware patch and tooling work remains reachable through these
tags, as well as the dormant `wave-firmware-experiments` branch. No proprietary
firmware binary is included in this archive or the active importer.

## Native component histories

`native/` contains Git bundles for the separate owning repositories. Each
component's JSON file records its official source URL, required public base,
bundle checksum, preserved refs and retained contribution branch.

These bundles preserve the exact custom commits, including the previous
individual topic branches. Public upstream history is excluded to avoid
duplicating large source repositories. To restore a component:

1. Clone its official repository and fetch the base commit named in its JSON.
2. Run `git bundle verify /path/to/component.bundle` inside that clone.
3. Fetch the bundle's branch refs, for example:
   `git fetch /path/to/component.bundle 'refs/heads/*:refs/heads/restored/*'`.
4. Recover historical tag refs similarly, or select any recorded commit.

Refs already contained in the public base are recorded as such rather than
duplicating their objects in the bundle. The Linux contribution stays based on
the OpenWrt-selected Linux 6.18.55 source. Native contribution histories need a
fresh owning-upstream rebase and review before submission.

## Validation boundary

The reduced port was rebased onto OpenWrt `0d245fdaeae5`. Its obsolete fstools
completion backport was removed because the new upstream pin includes it; the
separate selected-bank reset fix remains. That selected fstools package was
rebuilt successfully. Unchanged kernel, WAVE, importer and upgrade-source
validation was reused.

The grouped branches preserve optional work for separate review and testing.
Source composition checks do not claim a new complete image build, device
qualification or throughput measurement. This consolidation performed no
device or host-network changes and submitted nothing to official upstreams.
