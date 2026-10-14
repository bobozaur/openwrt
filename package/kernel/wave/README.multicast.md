Native multicast group IDs have separate host-table, pending-command, applied
firmware-membership and transmit-descriptor owners. Reuse requires all owners
to retire. An ID with uncertain firmware membership remains quarantined until a
fresh hardware probe, including against allocation for the same address.

Commands retain the exact station incarnation and run on the radio master that
owns station/SID removal. A first successful or uncertain JOIN transfers its pin
to the applied-membership owner; confirmed REMOVE releases that owner. Station,
VAP and terminal card teardown fence admission and cancel pending work before
freeing lifetime dependencies. A message-allocation failure is known unsent:
JOIN transfers its pin back to the pending command, LEAVE retains its applied
owner, and the exact command retries from completion with a 20ms delay. Higher
priority teardown can cancel it between attempts. Entering transport makes a
failure uncertain; required retirement that cannot finish quiesces the card. The applied list provides an allocation-free
retirement walk, independent of station-hash enumeration. Only the firmware's
individual REMOVE operation is used.

Native TX lookup acquires a pin while the membership table is locked. Ownership
moves to the descriptor and ends on submission failure, DAT_CFM or terminal
hardware drain, including completions for withdrawn VAPs. DirectConnect may not
admit retained group IDs without this native completion owner.

AP activation reopens admission only after its final checks and beacon submission.
Whole-card stop and startup unwind stop new data indications and synchronize
receive processing before multicast host tables and locks are destroyed. This
preserves control confirmations needed to retire memberships. Native in-place
recovery remains disabled; uncertain firmware outcomes terminally quiesce the
card and require a fresh probe.

This lifetime contract does not add VLAN identity or IGMPv3/MLDv2 source-filter
semantics. Tagged link-local exceptions and existing source-filter limitations
remain separate responsibilities.
