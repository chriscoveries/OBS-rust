/* Test-only oracle: the layout of struct encoder_packet from
 * libobs/obs-encoder.h, for the layout test of the Rust declaration. */
#include <stddef.h>

#include "obs.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_encoder_packet_size(void);
size_t oracle_encoder_packet_align(void);
size_t oracle_sizeof_long(void);
size_t oracle_encoder_packet_offset_data(void);
size_t oracle_encoder_packet_offset_size(void);
size_t oracle_encoder_packet_offset_pts(void);
size_t oracle_encoder_packet_offset_dts(void);
size_t oracle_encoder_packet_offset_timebase_num(void);
size_t oracle_encoder_packet_offset_timebase_den(void);
size_t oracle_encoder_packet_offset_type(void);
size_t oracle_encoder_packet_offset_keyframe(void);
size_t oracle_encoder_packet_offset_dts_usec(void);
size_t oracle_encoder_packet_offset_sys_dts_usec(void);
size_t oracle_encoder_packet_offset_priority(void);
size_t oracle_encoder_packet_offset_drop_priority(void);
size_t oracle_encoder_packet_offset_track_idx(void);
size_t oracle_encoder_packet_offset_encoder(void);

size_t oracle_encoder_packet_size(void)
{
	return sizeof(struct encoder_packet);
}
size_t oracle_encoder_packet_align(void)
{
	return _Alignof(struct encoder_packet);
}
size_t oracle_sizeof_long(void)
{
	return sizeof(long);
}
size_t oracle_encoder_packet_offset_data(void)
{
	return offsetof(struct encoder_packet, data);
}
size_t oracle_encoder_packet_offset_size(void)
{
	return offsetof(struct encoder_packet, size);
}
size_t oracle_encoder_packet_offset_pts(void)
{
	return offsetof(struct encoder_packet, pts);
}
size_t oracle_encoder_packet_offset_dts(void)
{
	return offsetof(struct encoder_packet, dts);
}
size_t oracle_encoder_packet_offset_timebase_num(void)
{
	return offsetof(struct encoder_packet, timebase_num);
}
size_t oracle_encoder_packet_offset_timebase_den(void)
{
	return offsetof(struct encoder_packet, timebase_den);
}
size_t oracle_encoder_packet_offset_type(void)
{
	return offsetof(struct encoder_packet, type);
}
size_t oracle_encoder_packet_offset_keyframe(void)
{
	return offsetof(struct encoder_packet, keyframe);
}
size_t oracle_encoder_packet_offset_dts_usec(void)
{
	return offsetof(struct encoder_packet, dts_usec);
}
size_t oracle_encoder_packet_offset_sys_dts_usec(void)
{
	return offsetof(struct encoder_packet, sys_dts_usec);
}
size_t oracle_encoder_packet_offset_priority(void)
{
	return offsetof(struct encoder_packet, priority);
}
size_t oracle_encoder_packet_offset_drop_priority(void)
{
	return offsetof(struct encoder_packet, drop_priority);
}
size_t oracle_encoder_packet_offset_track_idx(void)
{
	return offsetof(struct encoder_packet, track_idx);
}
size_t oracle_encoder_packet_offset_encoder(void)
{
	return offsetof(struct encoder_packet, encoder);
}
