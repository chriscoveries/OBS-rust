/* Test-only oracle: libobs/util/darray.h is header-only (static inline), so
 * this file exposes non-inline oracle_* wrappers that call the real header
 * functions, plus the struct layout as the C compiler sees it. */
#include <stddef.h>

#include "util/darray.h"

/* Test-only exports called from Rust over FFI. */
void oracle_darray_free(struct darray *da);
void oracle_darray_reserve(size_t es, struct darray *da, size_t capacity);
void oracle_darray_ensure_capacity(size_t es, struct darray *da, size_t new_size);
void oracle_darray_resize(size_t es, struct darray *da, size_t size);
void oracle_darray_clear(struct darray *da);
size_t oracle_darray_push_back_array(size_t es, struct darray *da, const void *array, size_t num);
void oracle_darray_erase(size_t es, struct darray *da, size_t idx);
void oracle_darray_pop_back(size_t es, struct darray *da);
size_t oracle_darray_size(void);
size_t oracle_darray_align(void);
size_t oracle_darray_offset_array(void);
size_t oracle_darray_offset_num(void);
size_t oracle_darray_offset_capacity(void);

void oracle_darray_free(struct darray *da)
{
	darray_free(da);
}
void oracle_darray_reserve(size_t es, struct darray *da, size_t capacity)
{
	darray_reserve(es, da, capacity);
}
void oracle_darray_ensure_capacity(size_t es, struct darray *da, size_t new_size)
{
	darray_ensure_capacity(es, da, new_size);
}
void oracle_darray_resize(size_t es, struct darray *da, size_t size)
{
	darray_resize(es, da, size);
}
void oracle_darray_clear(struct darray *da)
{
	darray_clear(da);
}
size_t oracle_darray_push_back_array(size_t es, struct darray *da, const void *array, size_t num)
{
	return darray_push_back_array(es, da, array, num);
}
void oracle_darray_erase(size_t es, struct darray *da, size_t idx)
{
	darray_erase(es, da, idx);
}
void oracle_darray_pop_back(size_t es, struct darray *da)
{
	darray_pop_back(es, da);
}

/* Layout of struct darray. */
size_t oracle_darray_size(void)
{
	return sizeof(struct darray);
}
size_t oracle_darray_align(void)
{
	return _Alignof(struct darray);
}
size_t oracle_darray_offset_array(void)
{
	return offsetof(struct darray, array);
}
size_t oracle_darray_offset_num(void)
{
	return offsetof(struct darray, num);
}
size_t oracle_darray_offset_capacity(void)
{
	return offsetof(struct darray, capacity);
}
