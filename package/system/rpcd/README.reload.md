# Native reload and session ownership

HUP and USR1 are handled through the existing uloop signal mechanism. Session
serialization runs after the active event callback returns, since it allocates
memory and shares the session tree and blob buffer with ordinary RPC methods.
HUP retains the native freeze/exec/thaw reload; USR1 retains freeze-and-exit.
No second session store, lifecycle service or authentication authority is added.

An init reload submits the signal; it does not acknowledge completion of the
replacement ubus object registration. Consumers provisioning a role should
observe the native service/object lifecycle before issuing their first request.
A failed request before login created no session, while an attempted login still
requires explicit owned-session reconciliation even if its reply was lost.

The local wireless enrollment transaction also accepts an exact desired raw
section as its expected snapshot. In that form both accepted alternatives are
identical: it may confirm, synchronize and notify already-published credentials,
but cannot publish them over the original pre-enrollment snapshot. Option absence,
string/list representation, the radio snapshot and pending/Apply/runtime-override
checks remain significant. A retired caller may use this restricted replay to
resolve an ambiguous reply; a runtime snapshot alone is not durable acceptance.
