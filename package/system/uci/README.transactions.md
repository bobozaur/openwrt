# Conditional configuration updates

`uci_lock()` and `uci_unlock()` let an existing configuration owner perform a
read/check/commit transaction on one file-backed package. Reload committed state
after locking; a previously loaded package may be stale. `uci_trylock(ctx, name)`
acquires a named package before any load, using the context's normal primary or
secondary configuration directory. It does not create a missing package or wait
for a lock. Contention, an interrupted lock attempt, or inode replacement returns
`UCI_ERR_BUSY`; it makes only one acquisition attempt. Ordinary saves, commits,
and reverts retain blocking semantics.

A context can nest locks for the same path; it cannot acquire a second package
while holding the first. A blocking waiter revalidates the locked inode against
its path before proceeding. Configuration publication retains the old and
replacement inode locks through rename, then transfers the context's ownership
to the new inode. Closing a stream does not explicitly unlock a description still
owned by the context. Configuration files can be read-only when their directory
permits atomic replacement. Direct editors bypassing libuci do not participate
in this protocol.

Saving copies existing raw delta records and appends the new records into a
replacement file. Export, flush, mode preservation, file synchronization and
close must succeed before rename. The original saved file and the in-memory
pending records survive any failure before that publication. After rename,
ownership of the new records transfers to the saved file; a directory-sync
failure does not leave duplicate records queued for a retry.

A normal commit first saves its local edits and reloads current configuration
under the package lock. It retains the caller's original package until loading
its replacement succeeds. An overwrite commit publishes the supplied snapshot;
it does not replay deltas already represented in that snapshot. Configuration
publication checks export, flush, mode preservation, file synchronization, close,
rename and directory synchronization. Only then are saved deltas consumed. An
error before configuration publication leaves saved edits available and retains
a usable in-memory package.

`UCI_ERR_PUBLISHED` distinguishes an error after publication from an unpublished
write failure. During save it means delta-file publication; during commit it can
also originate from that prerequisite save. Configuration publication followed by
a synchronization or delta-cleanup failure can leave both the new configuration
and its pending delta records present. Callers must inspect committed and pending
state before retrying non-idempotent changes such as list additions. This is not
a power-fail transaction spanning the configuration and delta files.

The context layout and error interface change the library ABI. Consumers must use
the matching library cohort. Saved-delta paths and the condition to check belong
to each configuration owner; package locking alone does not decide their policy.

The local `uci.wireless_enroll` rpcd operation owns the WPS conditional update. Its
inputs are `section`, `device`, complete raw `expected` and `expected_device`
section snapshots, and credential `values`. The optional `mode` defaults to
`sta`; AP enrollment must explicitly request `mode=ap`. Raw snapshots retain
option absence, string/list representation and list order. The committed
wifi-iface mode must match the requested mode and be bound to the named radio by
a string or an exact one-element list. Multiple radios are unsupported.
Credential values are limited to SSID, encryption, key, PMF and removal of an old
SAE-password override. AP mode additionally requires `wps_state` to be the string
`2`, retaining the configured WPS state after reload; STA mode rejects that field.

The operation rejects HTTP RPC sessions, defers an active Apply/rollback owner,
and acquires the persistent wireless package without waiting before loading it.
It uses a delta-free cursor with the secondary directory disabled. Pending native
or rpcd-session wireless deltas, an active `/var/run/uci/wireless` override, and
errors inspecting those owners return `busy`. A changed interface/radio snapshot
returns `conflict`. It returns `saved` only after durable commit and the normal
`config.change` notification attempt. A retry whose complete desired snapshot is
already present still rewrites it durably and notifies the reload owner, covering
a lost reply or an earlier post-rename synchronization failure.

The native enrollment caller submits the accepted authentication/protocol prefix
(`psk`, `psk2`, `psk-mixed`, `sae` or `sae-mixed`) with the negotiated supported
cipher (`tkip`, `ccmp`, `gcmp`, `ccmp256` or `gcmp256`). SAE prefixes reject TKIP;
open credentials use `none` and a null key. PSK `tkip+ccmp` profiles also remain
accepted. This reconnect profile retains the accepted authentication/protocol
policy with the selected cipher; it does not represent every cipher added to the
network by the driver.
