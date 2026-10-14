CCP retry ownership
===================

CCP prepares the kernel receive compressor while constructing a Configure-Request.
A timer retry constructs that request again and can replace a receiver that an
incoming Configure-Ack has already initialized. An earlier Ack can still be
queued for pppd at that point. If the retry reuses its identifier, that Ack can
open CCP while the replacement receiver is still stopped; a later Ack is then
ignored by the kernel's already-open CCP path.

The CCP retransmit callback allocates the next native FSM identifier before the
replacement request is constructed. The existing Ack/Nak/Reject checks therefore
accept replies only for the current request. This covers repeated retries for
MPPE and the other CCP receive-compression methods, while retaining the FSM's
retry count and timeout. Initial negotiation, peer-request identifier echo,
Reset-Request handling and other PPP control protocols keep their existing
owners. No key, encryption or MTU adjustment is part of this change.

[RFC 1661 sections 5.1 and 5.2](https://www.rfc-editor.org/rfc/rfc1661#section-5.1)
allow a new identifier on retransmission and require a Configure-Ack to match the
last request. The correction must run on the endpoint that rebuilds its receiver;
patching only the opposite endpoint cannot repair a counterpart's stale-Ack
acceptance. A delayed-CCP check should retain one call, verify the new request IDs
and current receive initialization, and send payload only after actual readiness.
