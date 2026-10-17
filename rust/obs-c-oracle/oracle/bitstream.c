/* Test-only oracle: the original libobs/util/bitstream.c, unmodified, with
 * every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. */
#include <stddef.h>

#define bitstream_reader_init oracle_bitstream_reader_init
#define bitstream_reader_read_bit oracle_bitstream_reader_read_bit
#define bitstream_reader_read_bits oracle_bitstream_reader_read_bits
#define bitstream_reader_r8 oracle_bitstream_reader_r8
#define bitstream_reader_r16 oracle_bitstream_reader_r16

#include "util/bitstream.c.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_bitstream_reader_size(void);
size_t oracle_bitstream_reader_align(void);
size_t oracle_bitstream_reader_offset_pos(void);
size_t oracle_bitstream_reader_offset_subPos(void);
size_t oracle_bitstream_reader_offset_buf(void);
size_t oracle_bitstream_reader_offset_len(void);

/* Layout of struct bitstream_reader as the C compiler sees the real header. */
size_t oracle_bitstream_reader_size(void)
{
	return sizeof(struct bitstream_reader);
}
size_t oracle_bitstream_reader_align(void)
{
	return _Alignof(struct bitstream_reader);
}
size_t oracle_bitstream_reader_offset_pos(void)
{
	return offsetof(struct bitstream_reader, pos);
}
size_t oracle_bitstream_reader_offset_subPos(void)
{
	return offsetof(struct bitstream_reader, subPos);
}
size_t oracle_bitstream_reader_offset_buf(void)
{
	return offsetof(struct bitstream_reader, buf);
}
size_t oracle_bitstream_reader_offset_len(void)
{
	return offsetof(struct bitstream_reader, len);
}
