/* SPDX-License-Identifier: GPL-2.0-only */
#include "twt-hw.h"
#include "twt-limits.h"

#define TWT_TEXT __attribute__((section(".twt.text")))
#define TWT_LIMIT_MASK 0xfff8U
#define TWT_LIMIT_MAX (2700U << 2)
#define TWT_LIMIT_READY 0x54574c31U
#define TWT_DATA_TIDS 8U

struct twt_limit_peer {
	unsigned short native[TWT_DATA_TIDS];
	unsigned active;
};

struct twt_limits {
	unsigned ready;
	struct twt_limit_peer peers[WAVE_TWT_STATIONS];
};

/* One UMAC reservation, accessed by every CPU through the same peripheral
 * IRAM address. No core accesses its cached low alias or clears it from its
 * private CRT. The linker separately bounds it and reserves it from ThreadX.
 */
extern volatile struct twt_limits wave_twt_limits_shared;
extern void native_twt_limit_acquire(unsigned semaphore);
extern void native_twt_limit_release(unsigned semaphore);
extern void native_twt_limit_assert(unsigned group, unsigned file,
				    unsigned line);

_Static_assert(sizeof(struct twt_limits) == 0x1404, "shared limits ABI");

static inline __attribute__((always_inline)) void twt_limit_barrier(void)
{
	__asm__ volatile ("dmb 3" : : : "memory");
}

static inline __attribute__((always_inline)) volatile unsigned *
twt_limit_row(unsigned sid)
{
	return (volatile unsigned *)(0xa032dd60U + 220U * sid);
}

static TWT_TEXT void twt_limit_lock(void)
{
	/* The native semaphore also saves and masks local interrupts. */
	native_twt_limit_acquire(1);
	twt_limit_barrier();
}

static TWT_TEXT void twt_limit_unlock(void)
{
	twt_limit_barrier();
	native_twt_limit_release(1);
}

static TWT_TEXT unsigned twt_limit_compose(volatile struct twt_limit_peer *peer,
					 unsigned active, unsigned tid,
					 unsigned encoded)
{
	if (active && tid < TWT_DATA_TIDS) {
		peer->native[tid] = encoded;
		if (encoded > TWT_LIMIT_MAX)
			encoded = TWT_LIMIT_MAX;
	}
	return encoded;
}

/* Replacement for the complete native setter, including its semaphore.
 * Native ATF/rate writers retain their existing last-writer policy. Only
 * the published data-TID limit is capped while TWT owns the station.
 * The lower CPU's ninth entry has no TWT overlay.
 */
TWT_TEXT void wave_twt_native_limit(unsigned sid, unsigned value)
{
	volatile struct twt_limit_peer *peer;
	volatile unsigned *row;
	unsigned tid, active, encoded = (value << 2) & TWT_LIMIT_MASK;

	if (sid >= WAVE_TWT_STATIONS) {
#ifdef WAVE_TWT_LOWER
		native_twt_limit_assert(0x27, 9, __LINE__);
#else
		native_twt_limit_assert(0x40, 0, __LINE__);
#endif
		return;
	}
	row = twt_limit_row(sid);
	peer = &wave_twt_limits_shared.peers[sid];
	twt_limit_lock();
	active = wave_twt_limits_shared.ready == TWT_LIMIT_READY && peer->active;
#ifdef WAVE_TWT_LOWER
	for (tid = 0; tid < 9; tid++) {
#else
	for (tid = 0; tid < TWT_DATA_TIDS; tid++) {
#endif
		unsigned limit = twt_limit_compose(peer, active, tid, encoded);
		volatile unsigned *field = row + 36 + 2 * tid;

		*field = (*field & ~TWT_LIMIT_MASK) | limit;
	}
	twt_limit_unlock();
}

#ifdef WAVE_TWT_LOWER
/* The native station constructor writes the same nine limits directly.
 * Preserve its adjacent word and non-limit field initialization exactly,
 * and make the native default a current policy write under the same lock.
 */
TWT_TEXT void wave_twt_limit_peer_initialize(unsigned sid, unsigned mode)
{
	volatile struct twt_limit_peer *peer = &wave_twt_limits_shared.peers[sid];
	volatile unsigned *row = twt_limit_row(sid);
	unsigned tid, active;

	/* Its native caller obtains SID from a byte. */
	twt_limit_lock();
	active = wave_twt_limits_shared.ready == TWT_LIMIT_READY && peer->active;
	for (tid = 0; tid < 9; tid++) {
		unsigned limit = twt_limit_compose(peer, active, tid, 5400U << 2);
		volatile unsigned *field = row + 36 + 2 * tid;

		field[-1] &= 0xf000f000U;
		*field = (*field & 0xfe000000U) | ((mode << 1) & 6U) | limit;
	}
	twt_limit_unlock();
}
#else
extern void native_twt_shared_start(void);

TWT_TEXT void wave_twt_limits_start(void)
{
	unsigned sid;

	/* Both lower CPUs are held at the existing shared-memory handshake.
	 * Initialize before its request is published, after AUX_VOL is configured.
	 * Shadows are first read only after enable has filled every data-TID entry.
	 * Keep this reservation outside the low-alias extension CRT clear range.
	 */
	wave_twt_limits_shared.ready = 0;
	for (sid = 0; sid < WAVE_TWT_STATIONS; sid++)
		wave_twt_limits_shared.peers[sid].active = 0;
	twt_limit_barrier();
	wave_twt_limits_shared.ready = TWT_LIMIT_READY;
	twt_limit_barrier();
	native_twt_shared_start();
}

TWT_TEXT int wave_twt_limits_set(unsigned sid, unsigned active)
{
	volatile struct twt_limit_peer *peer;
	volatile unsigned *row;
	unsigned tid;

	if (sid >= WAVE_TWT_STATIONS || active > 1)
		return WAVE_TWT_HW_INVALID;
	peer = &wave_twt_limits_shared.peers[sid];
	row = twt_limit_row(sid);
	twt_limit_lock();
	if (wave_twt_limits_shared.ready != TWT_LIMIT_READY) {
		twt_limit_unlock();
		return WAVE_TWT_HW_UNCERTAIN;
	}
	if (peer->active != active) {
		for (tid = 0; tid < TWT_DATA_TIDS; tid++) {
			volatile unsigned *field = row + 36 + 2 * tid;
			unsigned word = *field;
			unsigned limit;

			if (active)
				limit = twt_limit_compose(peer, 1, tid,
							word & TWT_LIMIT_MASK);
			else
				limit = peer->native[tid];
			*field = (word & ~TWT_LIMIT_MASK) | limit;
		}
		peer->active = active;
	}
	twt_limit_unlock();
	return WAVE_TWT_HW_OK;
}
#endif
