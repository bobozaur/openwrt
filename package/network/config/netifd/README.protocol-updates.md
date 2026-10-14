# Live protocol configuration updates

A ucode protocol can define `config_update(old_json, new_json)` alongside a
renew handler. The callback runs in netifd and must only compare the serialized,
canonical protocol configurations. Returning `true` accepts the desired
configuration for the existing instance; returning anything else uses the normal
teardown/setup path.

Netifd calls this hook only for an up interface whose other device, protocol,
startup and configuration-state requirements permit keeping the instance. It
copies the accepted configuration into the protocol state and dispatches through
the existing renew queue. Rapid updates coalesce there. The callback does not
establish that the later protocol operation succeeded; the handler must report
errors and publish its resulting link/address/route state normally.

WireGuard uses this path for peer changes when every non-peer interface option
is unchanged. `wg syncconf` updates the peer set while preserving the interface
and unchanged cryptographic peers. Addresses and allowed-IP routes use netifd's
normal reconciliation, including retirement of removed routes. Interface key,
address, MTU, underlay and other interface option changes still use a full reload.

After applying a complete peer configuration and reading its effective endpoints,
WireGuard calls `clear_host_dependencies()` and republishes its current endpoint
dependencies. The old watches remain until that point. A rejected update reports
an error while keeping the live interface; it does not report successful apply.
Protocol notifications carry the current setup token, including this dependency
reset, so callbacks from a retired instance cannot mutate its replacement.
Missing tools, incomplete configuration writes, MTU/link command failures and
rejected notifications report protocol errors. During renewal those failures do
not request teardown of the existing interface. Generated private keys are
accepted only after native UCI persistence succeeds; after a commit error the
cursor is reloaded because the configuration may already have been published.

The reset retires dependency watches, not netifd's shared underlay host-route
cache. Off-link endpoint routes in that cache follow the underlay's existing
route refresh/flush lifetime and may be used by other protocols. Peer removal
retires its WireGuard allowed-IP routes without deleting shared underlay routes.

Interface policy-rule withdrawal uses the IPv6 state, device identity and route
tables recorded at installation. Device or interface configuration can change
before teardown runs, so current settings cannot identify the old rule reliably.
Each logical interface retains its own rule lifetime; interfaces sharing an L3
device may have identical-looking rules. Withdrawal does not flush other owners.

A live device `ipv6` change uses the existing device event and L3 interface
owners. Disabling the family withdraws active IPv6 addresses, routes, neighbors,
prefix assignments and policy rules, and publishes the resulting interface
update without tearing down a still-valid IPv4 configuration. Aliases follow
their underlying device. Removing an explicit setting uses the value captured
at device bring-up, rather than a stale option value.

Address updates retain native setup ordering: the shared address callback
installs addresses before the interface-up event enables configured routes.
Point-to-point routes can therefore resolve their protocol-provided gateway.
This early address publication checks the effective family setting, separately
from the IP settings enabled flag; an IPv6-disabled device still rejects IPv6
publication. Route and neighbor activation continues to follow both settings.

Netifd retains its desired addresses, routes and prefix allocations for a later
re-enable. External IPv6 snapshots are invalidated: their protocol must publish
fresh state, since netifd does not own those kernel addresses or routes. Local
ULA prefixes remain independent of an uplink; delegated prefixes follow their
uplink's family state. Normal prefix withdrawal keeps native address deprecation,
but an IPv6-disabled downlink cannot retain or re-add a deprecated address.
This event covers configuration changes applied by netifd, not arbitrary
out-of-band sysctl writes.

An unbridged hotplug interface replaces its previous main device before attaching
a new one. The old wireless device may disappear afterward; that removal cannot
detach its successor. Explicitly configured devices and bridge membership retain
their existing admission rules. This permits native cross-radio handover without
requiring an extra reload to acquire the new L3 device.
