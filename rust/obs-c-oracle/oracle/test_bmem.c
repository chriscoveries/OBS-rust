/* Test-only stand-in for libobs util/bmem.c, which needs base/platform.
 * Linked into every obs-util test binary so the oracle's darray and
 * array-serializer code can resolve bmalloc/brealloc/bfree. Delete when
 * util/bmem.c is ported.
 *
 * Deliberately includes no libobs header: bmem.h marks these functions
 * EXPORT/dllexport. */
#include <stdlib.h>

void *bmalloc(size_t size)
{
	void *ptr;
	if (!size)
		abort();
	ptr = malloc(size);
	if (!ptr)
		abort();
	return ptr;
}

void *brealloc(void *ptr, size_t size)
{
	if (!size)
		abort();
	ptr = realloc(ptr, size);
	if (!ptr)
		abort();
	return ptr;
}

void bfree(void *ptr)
{
	free(ptr);
}

/* Stand-in for libobs util/bmem.c's base_get_alignment, which reports the
 * allocator's alignment (ALIGNMENT, 32). Needed by the media-io oracles. */
int base_get_alignment(void)
{
	return 32;
}
