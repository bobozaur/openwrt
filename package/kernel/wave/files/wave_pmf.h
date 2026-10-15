/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WAVE_PMF_H
#define WAVE_PMF_H

#include <linux/spinlock.h>
#include <linux/wait.h>
#include <crypto/aes-cbc-macs.h>

/* AP BIP is software-owned; firmware continues to own unicast CCMP.
 * Configuration calls are serialized by the AP wiphy. The lock also covers
 * management TX from firmware/scan contexts. Each in-flight reference lasts
 * until DMA completion or terminal DMA fencing, including failed submissions.
 */
struct wave_pmf {
	spinlock_t lock;
	wait_queue_head_t drained;
	struct {
		struct aes_cmac_key cmac;
		u8 key[16];
		u64 ipn;
		unsigned int pending;
		bool valid;
	} keys[2];
	int default_key;
	bool enabled;
};

void wave_pmf_init(struct wave_pmf *pmf);
int wave_pmf_set_key(struct wave_pmf *pmf, unsigned int index,
		     const u8 *key, const u8 *seq);
int wave_pmf_get_key(struct wave_pmf *pmf, unsigned int index, u8 *key, u8 *seq);
int wave_pmf_del_key(struct wave_pmf *pmf, unsigned int index);
int wave_pmf_set_default(struct wave_pmf *pmf, unsigned int index);
/* Stop new protected TX before draining existing descriptors. Call reset only
 * after firmware VAP removal or terminal DMA fencing has completed. */
void wave_pmf_block(struct wave_pmf *pmf);
void wave_pmf_reset(struct wave_pmf *pmf);
/* Append BIP to an AP group robust frame. Returns the held slot (0/1), 2 if
 * no BIP is needed, or a negative errno. A held slot must be completed once. */
int wave_pmf_protect(struct wave_pmf *pmf, u8 *frame, size_t *len, size_t capacity);
void wave_pmf_complete(struct wave_pmf *pmf, unsigned int slot);

#endif
