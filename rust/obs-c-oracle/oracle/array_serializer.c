/* Test-only oracle: the original libobs/util/array-serializer.c, unmodified,
 * with every global symbol renamed to oracle_* so it can link next to the Rust
 * implementation. */
#include <stddef.h>

#define array_output_serializer_init oracle_array_output_serializer_init
#define array_output_serializer_free oracle_array_output_serializer_free
#define array_output_serializer_reset oracle_array_output_serializer_reset

#include "util/array-serializer.c.h"

/* Test-only exports called from Rust over FFI. */
size_t oracle_serializer_size(void);
size_t oracle_serializer_align(void);
size_t oracle_serializer_offset_data(void);
size_t oracle_serializer_offset_read(void);
size_t oracle_serializer_offset_write(void);
size_t oracle_serializer_offset_seek(void);
size_t oracle_serializer_offset_get_pos(void);
size_t oracle_array_output_data_size(void);
size_t oracle_array_output_data_align(void);
size_t oracle_array_output_data_offset_bytes(void);
size_t oracle_array_output_data_offset_cur_pos(void);

/* Layout of struct serializer as the C compiler sees the real header. */
size_t oracle_serializer_size(void)
{
	return sizeof(struct serializer);
}
size_t oracle_serializer_align(void)
{
	return _Alignof(struct serializer);
}
size_t oracle_serializer_offset_data(void)
{
	return offsetof(struct serializer, data);
}
size_t oracle_serializer_offset_read(void)
{
	return offsetof(struct serializer, read);
}
size_t oracle_serializer_offset_write(void)
{
	return offsetof(struct serializer, write);
}
size_t oracle_serializer_offset_seek(void)
{
	return offsetof(struct serializer, seek);
}
size_t oracle_serializer_offset_get_pos(void)
{
	return offsetof(struct serializer, get_pos);
}

/* Layout of struct array_output_data. */
size_t oracle_array_output_data_size(void)
{
	return sizeof(struct array_output_data);
}
size_t oracle_array_output_data_align(void)
{
	return _Alignof(struct array_output_data);
}
size_t oracle_array_output_data_offset_bytes(void)
{
	return offsetof(struct array_output_data, bytes);
}
size_t oracle_array_output_data_offset_cur_pos(void)
{
	return offsetof(struct array_output_data, cur_pos);
}
