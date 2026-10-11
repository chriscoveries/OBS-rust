#include <stdlib.h>
#include <string.h>
#include <cmocka.h>
#include "librtmp/rtmp_sys.h"
#include "librtmp/amf.h"
#include "librtmp/log.h"

/* AMF3 class names (AVals) are length-prefixed, not NUL-terminated.
 * Logging one with %s reads past the buffer end. */
static void classname_at_buffer_end(void **state)
{
	(void)state;
	/* traits byte 0x03: dynamic=0, externalizable=0, 0 members;
     * class name len=4 sits at the very end of the allocation. */
	const char tmpl[] = {AMF_AVMPLUS, AMF3_OBJECT, 0x03, 0x09, 'n', 'a', 'm', 'e'};
	size_t n = sizeof(tmpl);
	char *buf = malloc(n);
	assert_non_null(buf);
	memcpy(buf, tmpl, n);

	AMFObject obj;
	int res = AMF_Decode(&obj, buf, (int)n, FALSE);
	(void)res;
	AMF_Reset(&obj);
	free(buf);
}

int main(void)
{
	RTMP_LogSetLevel(RTMP_LOGDEBUG);
	const struct CMUnitTest tests[] = {cmocka_unit_test(classname_at_buffer_end)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
