// SPDX-License-Identifier: GPL-2.0-only
#include "mtlkinc.h"
#include <linux/errno.h>
#include <linux/ieee80211.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include "wave_pmf.h"

#define WAVE_BIP_IPN_MAX ((1ULL << 48) - 1)
#define WAVE_BIP_MMIE_LEN 18

static void wave_bip_put_ipn(u8 *out, u64 ipn)
{
	unsigned int i;

	for (i = 0; i < 6; i++)
		out[i] = ipn >> (8 * i);
}

static u64 wave_bip_get_ipn(const u8 *seq)
{
	u64 ipn = 0;
	unsigned int i;

	if (seq)
		for (i = 0; i < 6; i++)
			ipn |= (u64)seq[i] << (8 * i);
	return ipn;
}

void wave_pmf_init(struct wave_pmf *pmf)
{
	memset(pmf, 0, sizeof(*pmf));
	spin_lock_init(&pmf->lock);
	init_waitqueue_head(&pmf->drained);
	pmf->default_key = -1;
}

int wave_pmf_del_key(struct wave_pmf *pmf, unsigned int index)
{
	unsigned long flags;
	unsigned int slot;

	if (index < 4 || index > 5)
		return -EINVAL;
	slot = index - 4;
	spin_lock_irqsave(&pmf->lock, flags);
	pmf->keys[slot].valid = false;
	if (pmf->default_key == slot)
		pmf->default_key = -1;
	spin_unlock_irqrestore(&pmf->lock, flags);
	if (!wait_event_timeout(pmf->drained, !READ_ONCE(pmf->keys[slot].pending),
				msecs_to_jiffies(2000)))
		return -ETIMEDOUT;
	spin_lock_irqsave(&pmf->lock, flags);
	memzero_explicit(&pmf->keys[slot], sizeof(pmf->keys[slot]));
	spin_unlock_irqrestore(&pmf->lock, flags);
	return 0;
}

int wave_pmf_set_key(struct wave_pmf *pmf, unsigned int index,
		     const u8 *key, const u8 *seq)
{
	unsigned long flags;
	unsigned int slot;
	u64 ipn = wave_bip_get_ipn(seq);
	int ret;
	bool was_default;

	if (index < 4 || index > 5)
		return -EINVAL;
	slot = index - 4;
	spin_lock_irqsave(&pmf->lock, flags);
	was_default = pmf->default_key == slot;
	if (pmf->keys[slot].valid && !memcmp(pmf->keys[slot].key, key, 16)) {
		/* A repeated installation must never reuse an already emitted IPN. */
		pmf->keys[slot].ipn = max(pmf->keys[slot].ipn, ipn);
		spin_unlock_irqrestore(&pmf->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&pmf->lock, flags);
	ret = wave_pmf_del_key(pmf, index);
	if (ret)
		return ret;
	spin_lock_irqsave(&pmf->lock, flags);
	aes_cmac_preparekey(&pmf->keys[slot].cmac, key, 16);
	memcpy(pmf->keys[slot].key, key, 16);
	pmf->keys[slot].ipn = ipn;
	pmf->keys[slot].valid = true;
	pmf->enabled = true;
	if (was_default)
		pmf->default_key = slot;
	spin_unlock_irqrestore(&pmf->lock, flags);
	return 0;
}

int wave_pmf_get_key(struct wave_pmf *pmf, unsigned int index, u8 *key, u8 *seq)
{
	unsigned long flags;
	unsigned int slot;
	int ret = 0;

	if (index < 4 || index > 5)
		return -EINVAL;
	slot = index - 4;
	spin_lock_irqsave(&pmf->lock, flags);
	if (!pmf->keys[slot].valid) {
		ret = -ENOENT;
	} else {
		memcpy(key, pmf->keys[slot].key, 16);
		wave_bip_put_ipn(seq, pmf->keys[slot].ipn);
	}
	spin_unlock_irqrestore(&pmf->lock, flags);
	return ret;
}

int wave_pmf_set_default(struct wave_pmf *pmf, unsigned int index)
{
	unsigned long flags;
	int ret = 0;

	if (index < 4 || index > 5)
		return -EINVAL;
	spin_lock_irqsave(&pmf->lock, flags);
	if (!pmf->keys[index - 4].valid)
		ret = -ENOENT;
	else
		pmf->default_key = index - 4;
	spin_unlock_irqrestore(&pmf->lock, flags);
	return ret;
}

void wave_pmf_block(struct wave_pmf *pmf)
{
	unsigned long flags;

	spin_lock_irqsave(&pmf->lock, flags);
	pmf->keys[0].valid = pmf->keys[1].valid = false;
	pmf->default_key = -1;
	spin_unlock_irqrestore(&pmf->lock, flags);
}

void wave_pmf_reset(struct wave_pmf *pmf)
{
	unsigned long flags;

	spin_lock_irqsave(&pmf->lock, flags);
	if (!WARN_ON(pmf->keys[0].pending || pmf->keys[1].pending)) {
		memzero_explicit(pmf->keys, sizeof(pmf->keys));
		pmf->default_key = -1;
		pmf->enabled = false;
	}
	spin_unlock_irqrestore(&pmf->lock, flags);
}

int wave_pmf_protect(struct wave_pmf *pmf, u8 *frame, size_t *len, size_t capacity)
{
	struct ieee80211_hdr *hdr = (void *)frame;
	struct aes_cmac_ctx ctx;
	unsigned long flags;
	u8 aad[20], mic[16], *mmie;
	int slot;

	if (*len < 24 || *len > capacity)
		return -EINVAL;
	if ((ieee80211_is_deauth(hdr->frame_control) ||
	     ieee80211_is_disassoc(hdr->frame_control)) && *len < 26)
		return -EINVAL;
	if (ieee80211_is_action(hdr->frame_control) && *len < 25)
		return -EINVAL;
	if (!is_multicast_ether_addr(hdr->addr1) ||
	    !_ieee80211_is_robust_mgmt_frame(hdr) ||
	    _ieee80211_is_group_privacy_action(hdr))
		return 2;
	spin_lock_irqsave(&pmf->lock, flags);
	if (!pmf->enabled) {
		slot = 2;
		goto out;
	}
	slot = pmf->default_key;
	if (slot < 0 || !pmf->keys[slot].valid) {
		slot = -ENOKEY;
		goto out;
	}
	if (*len > capacity || capacity - *len < WAVE_BIP_MMIE_LEN) {
		slot = -EMSGSIZE;
		goto out;
	}
	if (pmf->keys[slot].ipn == WAVE_BIP_IPN_MAX) {
		slot = -EOVERFLOW;
		goto out;
	}
	/* BIP authenticates plaintext; the Protected bit belongs to unicast CCMP. */
	hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	put_unaligned_le16(le16_to_cpu(hdr->frame_control) &
			   ~(IEEE80211_FCTL_RETRY | IEEE80211_FCTL_PM |
			     IEEE80211_FCTL_MOREDATA), aad);
	memcpy(aad + 2, &hdr->addrs, 3 * ETH_ALEN);
	mmie = frame + *len;
	memset(mmie, 0, WAVE_BIP_MMIE_LEN);
	mmie[0] = WLAN_EID_MMIE;
	mmie[1] = WAVE_BIP_MMIE_LEN - 2;
	put_unaligned_le16(slot + 4, mmie + 2);
	wave_bip_put_ipn(mmie + 4, ++pmf->keys[slot].ipn);
	*len += WAVE_BIP_MMIE_LEN;
	aes_cmac_init(&ctx, &pmf->keys[slot].cmac);
	aes_cmac_update(&ctx, aad, sizeof(aad));
	aes_cmac_update(&ctx, frame + 24, *len - 24);
	aes_cmac_final(&ctx, mic);
	memcpy(mmie + 10, mic, 8);
	memzero_explicit(mic, sizeof(mic));
	pmf->keys[slot].pending++;
out:
	spin_unlock_irqrestore(&pmf->lock, flags);
	return slot;
}

void wave_pmf_complete(struct wave_pmf *pmf, unsigned int slot)
{
	unsigned long flags;

	spin_lock_irqsave(&pmf->lock, flags);
	if (!WARN_ON(slot > 1 || !pmf->keys[slot].pending))
		pmf->keys[slot].pending--;
	spin_unlock_irqrestore(&pmf->lock, flags);
	wake_up_all(&pmf->drained);
}
