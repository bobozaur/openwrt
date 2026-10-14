/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WAVE_TWT_LIFETIME_H
#define WAVE_TWT_LIFETIME_H

#include "twt-hw.h"

#define WAVE_TWT_VAPS 32U
#define WAVE_TWT_FLOWS 8U
#define WAVE_TWT_SETUP_SIZE 20U

enum wave_twt_result {
	WAVE_TWT_BUSY = -4,
	WAVE_TWT_DISABLED = -5,
};

/* The protocol owner supplies an accepted individual agreement, with its
 * calendar phases already range-checked. The lifecycle assigns the flow ID
 * in the immutable response and owns its PD until actual TX completion.
 */
struct wave_twt_offer {
	unsigned start_tsf;
	unsigned low_phase;
	unsigned high_phase;
	unsigned char response[WAVE_TWT_SETUP_SIZE];
};

enum wave_twt_prepare_result {
	WAVE_TWT_PREPARED = 0,
	WAVE_TWT_REJECT = 1,
	WAVE_TWT_IGNORE = 2,
};

/* PREPARED supplies the accepted calendar/response. REJECT supplies only a
 * setup-rejection response; it must not enter the agreement admission path.
 * Malformed frames and received responses produce IGNORE without a reply.
 */
int wave_twt_prepare(unsigned band, const unsigned char *body, unsigned size,
		     struct wave_twt_offer *offer);

/* All entry points run in the same native RX task. Callbacks from other
 * tasks must be queued there. Peer admission is independent of old cancelled
 * responses: neither reconnect nor software reset may erase their records.
 */
int wave_twt_configure(unsigned band, unsigned vap, unsigned enabled);
int wave_twt_peer_enter(unsigned sid, unsigned band, unsigned vap);
int wave_twt_peer_leave(unsigned sid);
int wave_twt_offer_start(unsigned sid, const struct wave_twt_offer *offer);
int wave_twt_receive(unsigned sid, const unsigned char *body, unsigned size);
int wave_twt_teardown(unsigned sid, unsigned flow);
int wave_twt_reset(unsigned band);
int wave_twt_status(unsigned band, unsigned vap, unsigned char *enabled,
		    unsigned char *faulted);
void wave_twt_allocation_complete(unsigned cookie, void *pd);
int wave_twt_tx_complete(void *pd, unsigned success);

/* Native adapter boundary. Submission consumes the immutable body before
 * returning and eventually publishes one real PD completion. No immediate
 * synthetic success is permitted. The adapter also owns the native
 * station-record transition after agreement admission/last-member removal.
 */
void wave_twt_native_submit(void *pd, unsigned sid, unsigned vap,
			    const unsigned char *body, unsigned size);
void wave_twt_native_agreement(unsigned sid, unsigned active);

/* Extension-owned event, within the native allocator's ten-bit event field. */
#define WAVE_TWT_ALLOCATION_EVENT 0x3e0U
#define WAVE_TWT_RX_DESTINATION 19U

#endif
