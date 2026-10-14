/* SPDX-License-Identifier: GPL-2.0-only */
#include "twt-lifetime.h"

#define TWT_TEXT __attribute__((section(".twt.text")))
#define TWT_CATEGORY 22U
#define TWT_SETUP 6U
#define TWT_ELEMENT 216U
#define TWT_REQUEST 1U
#define TWT_TRIGGER (1U << 4)
#define TWT_IMPLICIT (1U << 5)
#define TWT_PROTECTION (1U << 15)
#define TWT_RX_DISABLED (1U << 4)

/* The A0 responder subtracts3168us from the advertised service period and
 * adds it to the low phase. Both phases are expressed in32us units. Keep
 * that native conversion explicit, and reject values it cannot represent
 * instead of silently truncating overflowing intervals or negative phases.
 */
#define TWT_PHASE_ADJUSTMENT 3168U
#define TWT_RESPONSE_LEAD 5000U
#define TWT_START_ALIGNMENT 16384U

static inline unsigned twt_le16(const unsigned char *p)
{
	return p[0] | ((unsigned)p[1] << 8);
}

static inline unsigned twt_le32(const unsigned char *p)
{
	return twt_le16(p) | (twt_le16(p + 2) << 16);
}

static inline void twt_put32(unsigned char *p, unsigned value)
{
	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

TWT_TEXT int wave_twt_prepare(unsigned band, const unsigned char *body,
			     unsigned size, struct wave_twt_offer *offer)
{
	unsigned request, command, exponent, mantissa, interval, duration;
	unsigned now_low, now_high, low, high, delta, old_low, response, i;
	int result;

	if (size < WAVE_TWT_SETUP_SIZE || body[0] != TWT_CATEGORY ||
	    body[1] != TWT_SETUP || body[3] != TWT_ELEMENT || body[4] != 15)
		return WAVE_TWT_IGNORE;
	request = twt_le16(body + 6);
	command = (request >> 1) & 7U;
	if (!(request & TWT_REQUEST) || command > 3)
		return WAVE_TWT_IGNORE;
	for (i = 0; i < WAVE_TWT_SETUP_SIZE; i++)
		offer->response[i] = body[i];
	offer->start_tsf = 0;
	offer->low_phase = 0;
	offer->high_phase = 0;
	response = (request & ~15U) | 14U;
	offer->response[6] = response;
	offer->response[7] = response >> 8;
	offer->response[5] = TWT_RX_DISABLED;

	/* The native responder implements individual implicit trigger-based
	 * agreements,256us wake-duration units and no protection/channel field.
	 * NDP, broadcast, grouping and other modes require different owners.
	 */
	if ((body[5] & ~TWT_RX_DISABLED) || command == 3 || body[19] ||
	    (request & TWT_PROTECTION) ||
	    (request & (TWT_TRIGGER | TWT_IMPLICIT)) !=
	    (TWT_TRIGGER | TWT_IMPLICIT))
		return WAVE_TWT_REJECT;
	duration = (unsigned)body[16] << 8;
	exponent = (request >> 10) & 31U;
	mantissa = twt_le16(body + 17);
	if (!mantissa || mantissa > (0xffffffffU >> exponent) ||
	    duration <= TWT_PHASE_ADJUSTMENT)
		return WAVE_TWT_REJECT;
	interval = mantissa << exponent;
	if (interval < duration || (interval & 31U))
		return WAVE_TWT_REJECT;
	delta = interval - duration;
	if (delta > WAVE_TWT_PHASE_MAX * 32U - TWT_PHASE_ADJUSTMENT)
		return WAVE_TWT_REJECT;
	offer->low_phase = (delta + TWT_PHASE_ADJUSTMENT) >> 5;
	offer->high_phase = (duration - TWT_PHASE_ADJUSTMENT) >> 5;
	result = wave_twt_hw_tsf(band, &now_low, &now_high);
	if (result)
		return result;

	if (command == 2) {
		low = twt_le32(body + 8);
		high = twt_le32(body + 12);
		delta = low - now_low;
		if (high - now_high - (low < now_low) ||
		    delta < TWT_RESPONSE_LEAD || delta > 0x7fffffffU)
			return WAVE_TWT_REJECT;
	} else {
		/* Native Request/Suggest policy: next16K boundary at least5ms
		 * ahead. Track carry explicitly without a runtime64-bit library.
		 */
		low = now_low + TWT_RESPONSE_LEAD;
		high = now_high + (low < now_low);
		old_low = low;
		low = (low + TWT_START_ALIGNMENT - 1) &
			~(TWT_START_ALIGNMENT - 1);
		high += low < old_low;
	}
	twt_put32(offer->response + 8, low);
	twt_put32(offer->response + 12, high);
	offer->start_tsf = low + duration - TWT_PHASE_ADJUSTMENT;
	if (command != 2)
		offer->start_tsf -= interval;
	response = (response & ~14U) | 8U;
	offer->response[6] = response;
	offer->response[7] = response >> 8;
	return WAVE_TWT_PREPARED;
}
