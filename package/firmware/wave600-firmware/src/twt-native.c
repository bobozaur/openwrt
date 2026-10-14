/* SPDX-License-Identifier: GPL-2.0-only */
#include "twt-lifetime.h"

#define TWT_TEXT __attribute__((section(".twt.text")))
#define TWT_RODATA __attribute__((section(".extension.rodata")))
#define TWT_TX_EVENT (WAVE_TWT_ALLOCATION_EVENT + 1)
#define TWT_CONTROL_EVENT (WAVE_TWT_ALLOCATION_EVENT + 2)
#define TWT_PD_SUBTYPE 22U
#define TWT_CONTROL_SET 0xa0U
#define TWT_CONTROL_GET 0xa1U
#define TWT_CONTROL_RESET 0xa2U

/* These imports are the actual old-cohort UMAC entries. Body +24, native
 * management type13 and software subtype22 follow the A0 descriptor path;
 * the new subtype is intercepted before its original unsupported branch.
 */
extern void *native_twt_message_allocate(unsigned size);
extern void native_twt_message_send(unsigned event, unsigned destination,
				    void *message, unsigned wait, unsigned vap);
extern void native_twt_management_submit(void *pd);
extern void native_twt_management_return(void *pd);
extern void native_twt_rx_dispatch(void *message);
extern void native_twt_strip_protection(void *pd);
extern void native_twt_rx_return(const unsigned *request);
extern unsigned native_twt_vap_band(unsigned vap);
extern void native_twt_peer_add(void *message);
extern void native_twt_peer_remove(void *message);
extern void native_twt_assert(unsigned group, unsigned file, unsigned line);

struct twt_dispatch_entry {
	void (*handler)(void *message);
	unsigned event;
};

extern void native_twt_dispatch(void *message,
				const struct twt_dispatch_entry *table,
				unsigned first, unsigned end);

_Static_assert(sizeof(struct twt_dispatch_entry) == 8, "native dispatch ABI");

TWT_TEXT void wave_twt_peer_added(void *message)
{
	const unsigned *words = message;
	const unsigned char *parent = (const unsigned char *)words[4];
	unsigned sid = *(const unsigned short *)(parent + 16);
	unsigned vap = parent[12];
	int result;

	/* UMI_STA_ADD flagsExt is payload byte63. The current host producer
	 * and actual reference both use bit1 for HE, not TWT requester support.
	 */
	if (parent[79] & 2)
		result = wave_twt_peer_enter(sid, native_twt_vap_band(vap), vap);
	else
		result = wave_twt_peer_leave(sid);
	if (result) {
		native_twt_assert(0x3a, 0, __LINE__);
		return;
	}
	native_twt_peer_add(message);
}

TWT_TEXT void wave_twt_peer_removed(void *message)
{
	const unsigned *words = message;
	const unsigned char *parent = (const unsigned char *)words[4];
	unsigned sid = *(const unsigned short *)(parent + 16);

	if (wave_twt_peer_leave(sid)) {
		/* Never acknowledge/reuse a SID after uncertain HW retirement,
		 * including builds where the native diagnostic can return.
		 */
		native_twt_assert(0x3a, 0, __LINE__);
		return;
	}
	native_twt_peer_remove(message);
}

TWT_TEXT void wave_twt_control_receive(void *message)
{
	volatile unsigned short *requests = (void *)0xb4994;
	const unsigned char *bytes = message;

	/* Same per-command request counter and retained host object as the
	 * admitted FIXED/RTS transport; host ingress is already serialized.
	 */
	(*requests)++;
	native_twt_message_send(TWT_CONTROL_EVENT, WAVE_TWT_RX_DESTINATION,
				message, 0, bytes[12]);
}

TWT_TEXT void wave_twt_native_submit(void *pd, unsigned sid, unsigned vap,
				     const unsigned char *body, unsigned size)
{
	unsigned *descriptor = pd;
	unsigned char *frame = (unsigned char *)descriptor[6];
	unsigned i;

	/* The native pool/constructor retains ownership of header flags,
	 * duration, sequence and addresses. The reference action producer
	 * copies only its body before invoking this same construction path.
	 */
	for (i = 0; i < size; i++)
		frame[24 + i] = body[i];
	descriptor[0] = (descriptor[0] & 0xffff4000U) | (24 + size);
	descriptor[1] = (descriptor[1] & 0xfffff000U) |
			(TWT_PD_SUBTYPE << 5) | (body[1] & 31U);
	descriptor[2] = (descriptor[2] & 0xf0ffffffU) | (13U << 24);
	descriptor[3] = (descriptor[3] & 0xe0800000U) |
			(9U << 24) | (vap << 13) | (sid << 4) | 15U;
	native_twt_management_submit(pd);
}

/* Replacement for just the type13/14 return-table entries. All existing
 * management subtypes keep their exact native consumer and release owner.
 */
TWT_TEXT void wave_twt_management_return(void *pd)
{
	const unsigned *descriptor = pd;
	unsigned *message;

	if (((descriptor[1] >> 5) & 127U) != TWT_PD_SUBTYPE) {
		native_twt_management_return(pd);
		return;
	}
	message = native_twt_message_allocate(4);
	message[4] = (unsigned)pd;
	native_twt_message_send(TWT_TX_EVENT, WAVE_TWT_RX_DESTINATION,
				message, 0, (descriptor[3] >> 13) & 127U);
}

TWT_TEXT void wave_twt_management_receive(void *pd, unsigned char *frame)
{
	unsigned *descriptor = pd;
	unsigned request[5];
	unsigned length, header, sid;

	length = descriptor[1] & 0x3fffU;
	if (length < 24)
		goto returned;
	header = 24 + ((frame[1] & 0x80) ? 4 : 0);
	if (length < header + 3 + ((frame[1] & 0x40) ? 16 : 0))
		goto returned;
	/* The original admission path has checked association and protection
	 * policy. This call is already in RX task19: event144, hardware queue
	 * table66a98, list walker5d41c and type table66ab0. Process it directly
	 * so one received list cannot fill the same task's queue with new work.
	 */
	if (frame[1] & 0x40) {
		/* The native helper itself returns undersized PDs to hardware.
		 * The bound above keeps that release path unreachable, leaving
		 * this adapter the sole return owner even for malformed actions.
		 */
		native_twt_strip_protection(pd);
		length -= 16;
	}
	sid = (descriptor[2] >> 4) & 511U;
	wave_twt_receive(sid, frame + header, length - header);
returned:
	/* The native list walker retained the next descriptor before its call.
	 * Return this one only after the protocol owner has copied the body.
	 */
	request[0] = 0x1003;
	request[1] = 0;
	request[2] = 0;
	request[3] = (unsigned)pd;
	request[4] = 0;
	native_twt_rx_return(request);
}

static TWT_TEXT void twt_allocated(void *message)
{
	const unsigned *words = message;
	const unsigned short *halves = message;

	wave_twt_allocation_complete(halves[10], (void *)words[4]);
}

static TWT_TEXT void twt_transmitted(void *message)
{
	const unsigned *words = message;
	unsigned *pd = (unsigned *)words[4];
	unsigned result = (pd[4] >> 8) & 15U;

	/* Preserve the A0 TWT responder's numeric completion convention. This
	 * is not a decoder for ordinary DATA ACK/retry telemetry.
	 */
	wave_twt_tx_complete(pd, result == 0 || result == 2);
}

static TWT_TEXT void twt_controlled(void *message)
{
	unsigned char *bytes = message;
	unsigned value = *(unsigned short *)(bytes + 16);
	unsigned vap = bytes[12];
	unsigned band = native_twt_vap_band(vap);
	unsigned operation = bytes[19];
	int result = WAVE_TWT_HW_INVALID;

	/* ABI1, zero input status/active/fault. Status values are extension
	 * owned:0 success,1 malformed,2 busy/disabled,3 uncertain hardware.
	 */
	if (*(unsigned *)(bytes + 20) == 0x100 && vap < WAVE_TWT_VAPS) {
		if (operation == TWT_CONTROL_SET && value <= 1)
			result = wave_twt_configure(band, vap, value);
		else if (operation == TWT_CONTROL_GET && !value)
			result = WAVE_TWT_HW_OK;
		else if (operation == TWT_CONTROL_RESET && !value)
			result = wave_twt_reset(band);
	}
	bytes[20] = result == WAVE_TWT_HW_OK ? 0 :
		result == WAVE_TWT_HW_INVALID ? 1 :
		result == WAVE_TWT_BUSY || result == WAVE_TWT_DISABLED ? 2 : 3;
	bytes[22] = 0;
	bytes[23] = 0;
	if (vap < WAVE_TWT_VAPS)
		wave_twt_status(band, vap, bytes + 22, bytes + 23);
	/* Existing persistent f7 completion owns the host147c confirmation
	 * and its counter. Reuse it instead of creating a second reply owner.
	 */
	native_twt_message_send(0xf7, 7, message, 0, vap);
}

static const struct twt_dispatch_entry twt_events[] TWT_RODATA = {
	{ twt_allocated, WAVE_TWT_ALLOCATION_EVENT },
	{ twt_transmitted, TWT_TX_EVENT },
	{ twt_controlled, TWT_CONTROL_EVENT },
};

/* Replace the RX task's callback pointer, retaining its existing queue,
 * stack, scheduler and generic event bookkeeping. No native task is added.
 */
TWT_TEXT void wave_twt_rx_dispatch(void *message)
{
	unsigned event = ((const unsigned short *)message)[3];

	if (event >= WAVE_TWT_ALLOCATION_EVENT && event <= TWT_CONTROL_EVENT)
		native_twt_dispatch(message, twt_events,
				    WAVE_TWT_ALLOCATION_EVENT, TWT_CONTROL_EVENT + 1);
	else
		native_twt_rx_dispatch(message);
}
