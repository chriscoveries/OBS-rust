#include "vt-frame-copy.h"

#include <stdatomic.h>
#include <string.h>

static _Atomic uint64_t total_bytes_copied = 0;

void vt_copy_bytes_reset(void)
{
	atomic_store(&total_bytes_copied, 0);
}

uint64_t vt_copy_bytes_total(void)
{
	return atomic_load(&total_bytes_copied);
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
