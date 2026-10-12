#include "vt-frame-copy.h"

#include <stdatomic.h>
#include <string.h>

static _Atomic uint64_t total_bytes_copied = 0;
static _Atomic uint64_t total_submissions = 0;
static _Atomic uint64_t used_submission_slots = 0;
static _Atomic uint64_t submission_limit = UINT64_MAX;
static _Atomic bool force_raw_for_test = false;

void vt_copy_bytes_reset(void)
{
	atomic_store(&total_bytes_copied, 0);
}

uint64_t vt_copy_bytes_total(void)
{
	return atomic_load(&total_bytes_copied);
}

void vt_encode_submission_reset(uint64_t limit)
{
	atomic_store(&total_submissions, 0);
	atomic_store(&used_submission_slots, 0);
	atomic_store(&submission_limit, limit);
}

uint64_t vt_encode_submission_total(void)
{
	return atomic_load(&total_submissions);
}

bool vt_encode_submission_reserve(void)
{
	uint64_t used = atomic_load(&used_submission_slots);
	for (;;) {
		uint64_t limit = atomic_load(&submission_limit);
		if (used >= limit)
			return false;
		if (atomic_compare_exchange_weak(&used_submission_slots, &used, used + 1))
			return true;
	}
}

void vt_encode_submission_commit(void)
{
	atomic_fetch_add(&total_submissions, 1);
}

void vt_encode_submission_cancel(void)
{
	atomic_fetch_sub(&used_submission_slots, 1);
}

void vt_test_force_raw(bool force_raw)
{
	atomic_store(&force_raw_for_test, force_raw);
}

bool vt_test_raw_forced(void)
{
	return atomic_load(&force_raw_for_test);
}

size_t vt_copy_frame_to_pixbuf(CVPixelBufferRef pixbuf, uint8_t *data[MAX_AV_PLANES], uint32_t linesize[MAX_AV_PLANES])
{
	size_t copied = 0;

	for (int i = 0; i < MAX_AV_PLANES; i++) {
		if (data[i] == NULL)
			break;
		uint8_t *p = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixbuf, i);
		uint8_t *f = data[i];
		size_t plane_linesize = CVPixelBufferGetBytesPerRowOfPlane(pixbuf, i);
		size_t plane_height = CVPixelBufferGetHeightOfPlane(pixbuf, i);

		for (size_t j = 0; j < plane_height; j++) {
			memcpy(p, f, linesize[i]);
			p += plane_linesize;
			f += linesize[i];
		}
		copied += linesize[i] * plane_height;
	}

	atomic_fetch_add(&total_bytes_copied, copied);
	return copied;
}
