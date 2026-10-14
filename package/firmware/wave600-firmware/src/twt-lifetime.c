/* SPDX-License-Identifier: GPL-2.0-only */
#include "twt-lifetime.h"

#define TWT_TEXT __attribute__((section(".twt.text")))
#define TWT_STATE __attribute__((section(".twt.state")))
#define TWT_CALENDARS (WAVE_TWT_BANDS * WAVE_TWT_GROUPS)
/* One additional response owner per band can reject an agreement when all
 * hardware groups are occupied. It never owns calendar or membership state.
 */
#define TWT_RECORDS (TWT_CALENDARS + WAVE_TWT_BANDS)
#define TWT_NO_HANDLE 0xffffU
#define TWT_PD_POOL 3U

enum twt_state {
	TWT_FREE,
	TWT_RESERVED,
	TWT_ALLOCATING,
	TWT_CANCELLED_ALLOCATION,
	TWT_TRANSMITTING,
	TWT_CANCELLED_TX,
	TWT_ACTIVE,
};

struct twt_record {
	void *pd;
	unsigned short handle;
	unsigned char sid;
	unsigned char vap;
	unsigned char flow;
	unsigned char state;
	unsigned char programmed;
	unsigned char member;
	unsigned char response[WAVE_TWT_SETUP_SIZE];
};

struct twt_peer {
	unsigned char band;
	unsigned char vap;
	unsigned char present;
};

/* One record per physical group. Keeping cancelled responses here also pins
 * their group and callback cookie. There is no generation wrap or timer-based
 * reuse, and no separate heap or full 256-by-eight response array.
 */
static struct twt_record records[TWT_RECORDS] TWT_STATE;
static struct twt_peer peers[WAVE_TWT_STATIONS] TWT_STATE;
static unsigned enabled_vaps[WAVE_TWT_BANDS] TWT_STATE;
static unsigned char ready[WAVE_TWT_BANDS] TWT_STATE;
static unsigned char fenced[WAVE_TWT_BANDS] TWT_STATE;

/* Exact native twelve-byte deferred-allocation request. */
struct twt_allocation {
	void *pd;
	unsigned short handle;
	unsigned short cookie;
	unsigned short event;
	unsigned char destination;
	unsigned char reserved;
};

_Static_assert(sizeof(struct twt_allocation) == 12, "native allocation ABI");
_Static_assert(sizeof(struct twt_record) == 32, "bounded group ownership");

extern unsigned native_twt_pd_allocate(unsigned pool,
				       struct twt_allocation *request);
extern void native_twt_pd_release(void *pd, unsigned pool);
extern void native_twt_request_delivered(unsigned handle, unsigned pool);
extern unsigned native_twt_request_cancel(unsigned handle, unsigned pool);
extern void native_critical_enter(unsigned *saved);
extern void native_critical_leave(unsigned saved);

static TWT_TEXT int twt_allocate_response(unsigned cookie);

static inline unsigned twt_band(unsigned cookie)
{
	return cookie < TWT_CALENDARS ? cookie / WAVE_TWT_GROUPS :
		cookie - TWT_CALENDARS;
}

static TWT_TEXT unsigned twt_members(unsigned band, unsigned sid)
{
	unsigned i, count = 0;

	for (i = band * WAVE_TWT_GROUPS;
	     i < (band + 1) * WAVE_TWT_GROUPS; i++)
		if (records[i].member && records[i].sid == sid)
			count++;
	return count;
}

static TWT_TEXT void twt_release_record(struct twt_record *record)
{
	if (record->state == TWT_RESERVED && !record->programmed &&
	    !record->pd && record->handle == TWT_NO_HANDLE)
		record->state = TWT_FREE;
}

static TWT_TEXT int twt_remove_calendar(unsigned cookie)
{
	struct twt_record *record = &records[cookie];
	unsigned band = twt_band(cookie);
	unsigned group = cookie % WAVE_TWT_GROUPS;
	int result;

	if (fenced[band] && (record->member || record->programmed))
		return WAVE_TWT_HW_UNCERTAIN;
	if (record->member) {
		result = wave_twt_hw_membership(band, group, record->sid, 0);
		if (result)
			goto uncertain;
		if (twt_members(band, record->sid) == 1) {
			result = wave_twt_hw_station(band, record->sid, 0, 0);
			if (result)
				goto uncertain;
			wave_twt_native_agreement(record->sid, 0);
		}
		/* Retain ownership until both membership and limit retirement
		 * succeed, so reset can retry either uncertain boundary.
		 */
		record->member = 0;
	}
	if (record->programmed) {
		result = wave_twt_hw_deactivate(band, group);
		if (result)
			goto uncertain;
		record->programmed = 0;
	}
	twt_release_record(record);
	return WAVE_TWT_HW_OK;

uncertain:
	fenced[band] = 1;
	return result;
}

static TWT_TEXT int twt_retire(unsigned cookie)
{
	struct twt_record *record = &records[cookie];
	unsigned saved, cancelled;

	switch (record->state) {
	case TWT_FREE:
		return WAVE_TWT_HW_OK;
	case TWT_ALLOCATING:
		/* Publish the tombstone before cancellation. A callback already
		 * handed to the task queue still owns this handle and record.
		 */
		record->state = TWT_CANCELLED_ALLOCATION;
		native_critical_enter(&saved);
		cancelled = native_twt_request_cancel(record->handle, TWT_PD_POOL);
		native_critical_leave(saved);
		if (cancelled) {
			record->handle = TWT_NO_HANDLE;
			record->state = TWT_RESERVED;
		}
		break;
	case TWT_TRANSMITTING:
		record->state = TWT_CANCELLED_TX;
		break;
	case TWT_ACTIVE:
		record->state = TWT_RESERVED;
		break;
	default:
		break;
	}
	return twt_remove_calendar(cookie);
}

TWT_TEXT int wave_twt_peer_leave(unsigned sid)
{
	unsigned i;
	int result = WAVE_TWT_HW_OK, next;

	if (sid >= WAVE_TWT_STATIONS)
		return WAVE_TWT_HW_INVALID;
	peers[sid].present = 0;
	for (i = 0; i < TWT_RECORDS; i++) {
		if (records[i].state == TWT_FREE || records[i].sid != sid)
			continue;
		next = twt_retire(i);
		if (next)
			result = next;
	}
	return result;
}

TWT_TEXT int wave_twt_peer_enter(unsigned sid, unsigned band, unsigned vap)
{
	int result;

	if (sid >= WAVE_TWT_STATIONS || band >= WAVE_TWT_BANDS ||
	    vap >= WAVE_TWT_VAPS)
		return WAVE_TWT_HW_INVALID;
	result = wave_twt_peer_leave(sid);
	if (result)
		return result;
	peers[sid].band = band;
	peers[sid].vap = vap;
	peers[sid].present = 1;
	return WAVE_TWT_HW_OK;
}

TWT_TEXT int wave_twt_configure(unsigned band, unsigned vap, unsigned enabled)
{
	unsigned i;
	int result = WAVE_TWT_HW_OK, next;

	if (band >= WAVE_TWT_BANDS || vap >= WAVE_TWT_VAPS || enabled > 1)
		return WAVE_TWT_HW_INVALID;
	if (enabled) {
		if (fenced[band])
			return WAVE_TWT_HW_UNCERTAIN;
		if (!ready[band]) {
			result = wave_twt_reset(band);
			if (result)
				return result;
		}
		enabled_vaps[band] |= 1U << vap;
		return WAVE_TWT_HW_OK;
	}
	enabled_vaps[band] &= ~(1U << vap);
	for (i = 0; i < TWT_RECORDS; i++) {
		struct twt_record *record = &records[i];
		unsigned notify;

		if (twt_band(i) != band || records[i].state == TWT_FREE ||
		    records[i].vap != vap)
			continue;
		notify = record->state == TWT_ACTIVE;
		next = twt_retire(i);
		if (next)
			result = next;
		else if (notify) {
			/* Hardware withdrawal is terminal. Notify the current peer
			 * using the now-retired record, with ordinary best-effort
			 * management delivery and the existing PD lifetime.
			 */
			record->response[0] = 22;
			record->response[1] = 7;
			record->response[2] = record->flow;
			twt_allocate_response(i);
		}
	}
	return result;
}

TWT_TEXT int wave_twt_status(unsigned band, unsigned vap, unsigned char *enabled,
			    unsigned char *faulted)
{
	if (band >= WAVE_TWT_BANDS || vap >= WAVE_TWT_VAPS)
		return WAVE_TWT_HW_INVALID;
	*enabled = !!(enabled_vaps[band] & (1U << vap));
	*faulted = fenced[band];
	return WAVE_TWT_HW_OK;
}

static TWT_TEXT void twt_submit(struct twt_record *record, void *pd)
{
	record->pd = pd;
	record->state = TWT_TRANSMITTING;
	wave_twt_native_submit(pd, record->sid, record->vap, record->response,
			       record->response[1] == 7 ? 3 : WAVE_TWT_SETUP_SIZE);
}

static TWT_TEXT int twt_allocate_response(unsigned cookie)
{
	struct twt_allocation request;
	struct twt_record *record = &records[cookie];
	unsigned saved, allocated;
	int result;

	request.pd = 0;
	request.reserved = 0;
	request.cookie = cookie;
	request.event = WAVE_TWT_ALLOCATION_EVENT;
	request.destination = WAVE_TWT_RX_DESTINATION;
	request.handle = TWT_NO_HANDLE;
	record->state = TWT_ALLOCATING;
	native_critical_enter(&saved);
	allocated = native_twt_pd_allocate(TWT_PD_POOL, &request);
	native_critical_leave(saved);
	if (allocated) {
		twt_submit(record, request.pd);
	} else if (request.handle != TWT_NO_HANDLE) {
		record->handle = request.handle;
	} else {
		record->state = TWT_RESERVED;
		result = twt_remove_calendar(cookie);
		return result ? result : WAVE_TWT_BUSY;
	}
	return (int)cookie;
}

TWT_TEXT int wave_twt_offer_start(unsigned sid, const struct wave_twt_offer *offer)
{
	struct twt_record *record;
	unsigned i, band, flow, used = 0, cookie = TWT_RECORDS;
	int result;

	if (sid >= WAVE_TWT_STATIONS || !offer ||
	    offer->low_phase > WAVE_TWT_PHASE_MAX ||
	    offer->high_phase > WAVE_TWT_PHASE_MAX)
		return WAVE_TWT_HW_INVALID;
	band = peers[sid].band;
	if (!peers[sid].present || fenced[band] ||
	    !(enabled_vaps[band] & (1U << peers[sid].vap)))
		return WAVE_TWT_DISABLED;
	for (i = 0; i < TWT_CALENDARS; i++) {
		if (records[i].state == TWT_FREE) {
			if (i / WAVE_TWT_GROUPS == band && cookie == TWT_RECORDS)
				cookie = i;
		} else if (records[i].sid == sid) {
			used |= 1U << records[i].flow;
			/* Announced type is a per-station selector bit. */
			if (records[i].programmed &&
			    ((records[i].response[6] ^ offer->response[6]) & 0x40))
				return WAVE_TWT_BUSY;
		}
	}
	for (flow = 0; flow < WAVE_TWT_FLOWS; flow++)
		if (!(used & (1U << flow)))
			break;
	if (cookie == TWT_RECORDS || flow == WAVE_TWT_FLOWS)
		return WAVE_TWT_BUSY;
	record = &records[cookie];
	record->pd = 0;
	record->handle = TWT_NO_HANDLE;
	record->sid = sid;
	record->vap = peers[sid].vap;
	record->flow = flow;
	record->member = 0;
	record->state = TWT_RESERVED;
	for (i = 0; i < WAVE_TWT_SETUP_SIZE; i++)
		record->response[i] = offer->response[i];
	record->response[6] = (record->response[6] & 0x7f) | ((flow & 1) << 7);
	record->response[7] = (record->response[7] & 0xfc) | (flow >> 1);
	/* Even a timed-out command owns the group until explicit reset. */
	record->programmed = 1;
	result = wave_twt_hw_program(band, cookie % WAVE_TWT_GROUPS,
				     offer->start_tsf, offer->low_phase,
				     offer->high_phase);
	if (result) {
		fenced[band] = 1;
		return result;
	}
	return twt_allocate_response(cookie);
}

TWT_TEXT void wave_twt_allocation_complete(unsigned cookie, void *pd)
{
	struct twt_record *record;
	unsigned saved;

	/* The adapter admits only our event and exact native cookie. */
	if (cookie >= TWT_RECORDS)
		return;
	record = &records[cookie];
	if (record->state != TWT_ALLOCATING &&
	    record->state != TWT_CANCELLED_ALLOCATION)
		return;
	native_critical_enter(&saved);
	native_twt_request_delivered(record->handle, TWT_PD_POOL);
	native_critical_leave(saved);
	record->handle = TWT_NO_HANDLE;
	if (record->state == TWT_ALLOCATING &&
	    (!record->programmed || !fenced[twt_band(cookie)])) {
		twt_submit(record, pd);
		return;
	}
	native_twt_pd_release(pd, TWT_PD_POOL);
	record->state = TWT_RESERVED;
	twt_release_record(record);
}

TWT_TEXT int wave_twt_tx_complete(void *pd, unsigned success)
{
	struct twt_record *record;
	unsigned cookie, band;
	int result = WAVE_TWT_HW_OK;

	for (cookie = 0; cookie < TWT_RECORDS; cookie++)
		if (records[cookie].pd == pd &&
		    (records[cookie].state == TWT_TRANSMITTING ||
		     records[cookie].state == TWT_CANCELLED_TX))
			break;
	if (cookie == TWT_RECORDS)
		return WAVE_TWT_HW_INVALID;
	record = &records[cookie];
	band = twt_band(cookie);
	if (record->state == TWT_TRANSMITTING && record->programmed &&
	    record->response[1] == 6 && !success) {
		/* As in mac80211's failed setup handling, withdraw the offered
		 * agreement and send one teardown. Reuse the returned PD, which
		 * the native management path explicitly supports. Teardown
		 * completion has no retry loop and can never add membership.
		 */
		result = twt_remove_calendar(cookie);
		record->response[0] = 22;
		record->response[1] = 7;
		record->response[2] = record->flow;
		twt_submit(record, pd);
		return result;
	}
	if (record->state == TWT_TRANSMITTING && record->programmed &&
	    record->response[1] == 6 && success && !fenced[band]) {
		/* A timed-out membership update may already have reached hardware. */
		record->member = 1;
		result = wave_twt_hw_membership(band, cookie % WAVE_TWT_GROUPS,
					       record->sid, 1);
		if (!result)
			result = wave_twt_hw_station(band, record->sid, 1,
						    !!(record->response[6] & 0x40));
		if (!result) {
			wave_twt_native_agreement(record->sid, 1);
			record->state = TWT_ACTIVE;
		} else {
			fenced[band] = 1;
			record->state = TWT_RESERVED;
		}
	} else {
		record->state = TWT_RESERVED;
		result = twt_remove_calendar(cookie);
	}
	record->pd = 0;
	native_twt_pd_release(pd, TWT_PD_POOL);
	twt_release_record(record);
	return result;
}

TWT_TEXT int wave_twt_teardown(unsigned sid, unsigned flow)
{
	unsigned i;

	if (sid >= WAVE_TWT_STATIONS || flow >= WAVE_TWT_FLOWS)
		return WAVE_TWT_HW_INVALID;
	for (i = 0; i < TWT_CALENDARS; i++)
		if (records[i].state != TWT_FREE && records[i].sid == sid &&
		    records[i].flow == flow)
			return twt_retire(i);
	return WAVE_TWT_HW_OK;
}

TWT_TEXT int wave_twt_receive(unsigned sid, const unsigned char *body, unsigned size)
{
	struct wave_twt_offer offer;
	struct twt_record *record;
	unsigned band, cookie, i;
	int result;

	if (sid >= WAVE_TWT_STATIONS || !peers[sid].present)
		return WAVE_TWT_DISABLED;
	band = peers[sid].band;
	if (!(enabled_vaps[band] & (1U << peers[sid].vap)))
		return WAVE_TWT_DISABLED;
	if (size >= 3 && body[0] == 22 && body[1] == 7) {
		if (body[2] & ~7U)
			return WAVE_TWT_IGNORE;
		return wave_twt_teardown(sid, body[2]);
	}
	result = wave_twt_prepare(band, body, size, &offer);
	if (result == WAVE_TWT_IGNORE || result < 0)
		return result;
	if (result == WAVE_TWT_PREPARED) {
		result = wave_twt_offer_start(sid, &offer);
		if (result >= 0)
			return result;
	}
	cookie = TWT_CALENDARS + band;
	record = &records[cookie];
	if (record->state != TWT_FREE)
		return WAVE_TWT_BUSY;
	record->pd = 0;
	record->handle = TWT_NO_HANDLE;
	record->sid = sid;
	record->vap = peers[sid].vap;
	record->flow = 0;
	record->member = 0;
	record->programmed = 0;
	record->state = TWT_RESERVED;
	for (i = 0; i < WAVE_TWT_SETUP_SIZE; i++)
		record->response[i] = offer.response[i];
	record->response[6] = (record->response[6] & ~15U) | 14U;
	return twt_allocate_response(cookie);
}

TWT_TEXT int wave_twt_reset(unsigned band)
{
	unsigned i;
	int result;

	if (band >= WAVE_TWT_BANDS)
		return WAVE_TWT_HW_INVALID;
	enabled_vaps[band] = 0;
	fenced[band] = 1;
	for (i = 0; i < TWT_RECORDS; i++)
		if (twt_band(i) == band)
			twt_retire(i);
	result = wave_twt_hw_quiesce(band);
	if (result)
		return result;
	for (i = band * WAVE_TWT_GROUPS;
	     i < (band + 1) * WAVE_TWT_GROUPS; i++) {
		struct twt_record *record = &records[i];

		if (!record->member)
			continue;
		result = wave_twt_hw_membership(band, i % WAVE_TWT_GROUPS,
					       record->sid, 0);
		if (result)
			return result;
		if (twt_members(band, record->sid) == 1) {
			result = wave_twt_hw_station(band, record->sid, 0, 0);
			if (result)
				return result;
			wave_twt_native_agreement(record->sid, 0);
		}
		record->member = 0;
	}
	result = wave_twt_hw_reset(band);
	if (result)
		return result;
	for (i = 0; i < TWT_RECORDS; i++) {
		struct twt_record *record = &records[i];

		if (twt_band(i) != band)
			continue;
		record->programmed = 0;
		/* Actual outstanding callbacks retain their state, handle and PD. */
		twt_release_record(record);
	}
	fenced[band] = 0;
	ready[band] = 1;
	return WAVE_TWT_HW_OK;
}
