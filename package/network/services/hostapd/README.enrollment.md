# WPS enrollment ownership

Credential delivery is not connection success. Each accepted native credential
event identifies its processed network and WPS run. The supplicant keeps these
candidates with the exact interface/configuration owner and chooses only the
network that reaches `COMPLETED` after successful WPS. Multiple credentials do
not become a last-event-wins configuration update.

Both Wi-Fi script backends carry the original UCI section name, radio name and
raw option snapshots. Generated/temporary VIFs without that persistent owner
cannot write UCI. Persistence requires a complete snapshot assigned to exactly
one radio; partial and multi-radio configurations are not advertised as
persistable. The button calls the interface's native WPS owner; Multi-AP
intent comes from its configuration unless a caller explicitly overrides it.
The shell backend uses native credential processing instead of a global event
listener. Dotted interface names are preserved.

The local rpcd `uci.wireless_enroll` operation serializes the pending-edit,
Apply/rollback and snapshot checks with publication. It uses committed persistent
configuration, defers active runtime overrides, and never consumes another
session's pending changes. The libuci package transaction also serializes native
CLI saves/commits; its nonblocking entry keeps contention from stalling rpcd.
Only SSID, encryption, key, PMF and the obsolete separate SAE password are updated
for STA enrollment. AP enrollment also commits the native configured WPS state.
Cipher persistence uses the actual negotiated cipher within the accepted native
policy. Driver-added optional GCMP/256-bit alternatives do not make an otherwise
representable connection fail enrollment. Embedded-NUL credentials cannot be
represented in UCI and are reported without truncating them.

Pending changes can defer persistence for up to two minutes after credential
receipt, with revalidation of the selected network and exact owner on each retry.
Replacement/removal, a new run, cancellation, timeout or overlap invalidates the
old candidate. Cancelling pending persistence after successful association keeps
that connection, including late or repeated cancellation after the candidate
has expired or been saved; it prevents a later callback from republishing the
cancelled run. Ordinary native WPS cancellation still cancels an unfinished handshake.

If a publication reply is ambiguous, the retiring interface can confirm the
old operation through the same local transaction before native configuration
replacement. This confirmation uses the exact desired raw section as its expected
state, with the original radio snapshot. It can only re-sync credentials already
published on disk; it cannot apply an unsaved candidate to the old configuration.
An existing replacement must match that complete desired section and radio owner.
The original interface resource, generation, WPS run, cancellation authority and
expiry still apply. No credential writer or new enrollment window transfers to
the replacement. Only a returned durable `saved` result confirms storage; a
failed confirmation reports `unconfirmed`, while an observed conflict remains a
conflict. Expiry or automatic retirement after an ambiguous publication also
retains that uncertainty instead of claiming the credentials were never saved.
Explicit cancellation still cancels the operation. Known busy replies do not
create this confirmation authority.

`wps_status` returns current native connection state and a credential-free,
bounded result of the last enrollment operation. The result survives the normal
configuration reload caused by saving credentials; it is not a cached connection
state or a configuration-write authority. A saved profile can reconnect without
the transient WPS run identifiers; late Cancel then leaves that connection and
the saved result intact. LuCI's wireless overview opens this
status for clients and provides writer-only Start/Cancel actions. A section ID
fences controls against interface-name reuse. Readers retain status access. The
methods accept the session metadata injected by authenticated ubus HTTP transport;
the existing transport ACLs retain read/write authorization. This does not expose
the local-only `uci.wireless_enroll` persistence operation over HTTP.

Full hostapd builds include the existing WPS UPnP registrar interface, and full
supplicant builds include the standard external registrar. The existing
`ext_registrar` setting determines AP UPnP exposure; enabling the compiled feature
does not enable it for every BSS. Native PIN/registrar commands remain available
through their existing control interfaces. An AP PIN must be active before an
external registrar discovers this AP: the native registrar fetches `GetDeviceInfo`
before subscribing and reporting `WPS-ER-AP-ADD`, and the AP requires its PIN for
that enrollee operation. For a bounded enrollment window, use the existing
`wps_ap_pin random <timeout>` control command, retain that PIN for LEARN or CONFIG,
and disable it when finished. UPnP discovery, learning credentials and configuring
the AP are separate results; enabling `ext_registrar` alone establishes none of
them.

For an AP managed through native inline configuration, received AP settings are
returned to the exact live BSS owner. Both Wi-Fi script backends pass the original
UCI interface and radio snapshots with `hostapd.config_set`. The credential callback
accepts only a representable personal/open profile, preserves configured PMF and
SAE-add policy, and invokes the existing local `uci.wireless_enroll` transaction
with explicit AP mode and configured WPS state. The native `wps_state` option is
preserved by both generators, so an accepted open profile remains configured after
reload. Its existing unconfigured-open default remains when no state is supplied.
STA callers retain their existing default mode and field checks.
Temporary BSSes without a complete persistent owner cannot publish credentials.

Protocol success follows the transaction's durable `saved` result. Busy, conflict,
unsupported credentials or a failed reply cause WPS failure; no deferred credential
is retained and no RAM-only configuration is reported as saved. The registered
ubus connection queues nested configuration calls until the WPS/SOAP callback
returns, after which normal netifd reload applies the committed settings. A saved
result establishes accepted configuration, not completed AP restart or client
authentication. Standalone file-backed hostapd and explicitly external credential
processing retain their existing behavior, including credential notifications
on receipt before local processing. Discovery, current-settings retrieval,
configuration acceptance and authentication with the new profile remain separate
validation results.
