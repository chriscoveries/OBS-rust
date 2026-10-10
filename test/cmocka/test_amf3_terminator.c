#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "librtmp/amf.h"

/* A spec-valid AMF3 document: an AMF0-encapsulated dynamic object holding
 * a nested dynamic object through member "a". librtmp reads a nested
 * object's marker twice — as member type and as object header — so each
 * nested level emits it twice. */
static void nested_dynamic_object(void **state)
{
	(void)state;

	const char doc[] = {
		AMF_AVMPLUS, AMF3_OBJECT, 0x0b, 0x01, 0x03,         'a',  AMF3_OBJECT, AMF3_OBJECT,
		0x0b,        0x01,        0x03, 'a',  AMF3_INTEGER, 0x00, 0x01, /* empty name ends the inner member list */
		0x01, /* empty name ends the outer member list */
	};
	AMFObject obj;
	AMFObjectProperty *prop;
	AMFObject inner;
	AVal a = AVC("a");
	int outer_count, inner_count, inner_has_a;

	assert_int_equal(AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE), (int)sizeof(doc));

	outer_count = AMF_CountProp(&obj);
	prop = AMF_GetProp(&obj, NULL, 0);
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);
	AMFProp_GetObject(prop, &inner);
	inner_count = AMF_CountProp(&inner);
	inner_has_a = AMF_GetProp(&inner, &a, -1) != NULL;

	/* on master this double-frees the inner object: the terminator was
	 * added with the stale union pointer from member "a" */
	AMF_Reset(&obj);

	/* the empty-name terminator is a list delimiter, not a property */
	assert_int_equal(outer_count, 1);
	assert_int_equal(inner_count, 1);
	assert_true(inner_has_a);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(nested_dynamic_object),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
