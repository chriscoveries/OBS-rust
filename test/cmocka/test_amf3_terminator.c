#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "librtmp/amf.h"

/* AMF_GetProp hands back a pointer to the shared AMFProp_Invalid sentinel
 * when the property is not present, never NULL, so a caller must check
 * AMFProp_IsValid rather than comparing against NULL. */
static void inner_property_lookup(void **state)
{
	(void)state;

	const char doc[] = {
		AMF_AVMPLUS, AMF3_OBJECT, 0x0b, 0x01,         0x03, 'a',  AMF3_OBJECT, 0x0b,
		0x01,        0x03,        'a',  AMF3_INTEGER, 0x00, 0x01, 0x01,
	};
	AMFObject obj;
	AMFObjectProperty *prop;
	AMFObject inner;
	AVal a = AVC("a");

	assert_int_equal(AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE), (int)sizeof(doc));

	prop = AMF_GetProp(&obj, NULL, 0);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);
	AMFProp_GetObject(prop, &inner);

	prop = AMF_GetProp(&inner, &a, -1);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);

	/* a missing property still returns the sentinel, not NULL */
	prop = AMF_GetProp(&inner, &(AVal)AVC("nope"), -1);
	assert_false(AMFProp_IsValid(prop));

	AMF_Reset(&obj);
}

/* A spec-valid flat dynamic object: the trait 0x0b is zero sealed members
 * plus dynamic, so the object has only dynamic members, and the empty name
 * that closes the member list must not be stored as a property of its own.
 * This is the document the AMF3 spec describes; the use-after-free itself is
 * reproduced by the nested document below. */
static void flat_dynamic_object_terminator(void **state)
{
	(void)state;

	const char doc[] = {
		AMF_AVMPLUS,  AMF3_OBJECT, 0x0b, /* no sealed members, dynamic */
		0x05,         'k',         0,    /* inline class name "k", NUL-terminated */
		0x03,         'a',               /* dynamic member name "a" */
		AMF3_INTEGER, 0x01,              /* member "a" = 1 */
		0x01,                            /* empty name closes the member list */
	};
	AMFObject obj;
	int count;

	assert_int_equal(AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE), (int)sizeof(doc));

	/* the terminator is a list delimiter, so exactly one property is kept */
	count = AMF_CountProp(&obj);
	assert_int_equal(count, 1);

	AMF_Reset(&obj);
}

/* A nested dynamic object previously appended the empty-name terminator
 * with the stale union pointer inherited from member "a". Keep that
 * use-after-free regression covered with the single-marker encoding (#288). */
static void nested_dynamic_object(void **state)
{
	(void)state;

	const char doc[] = {
		AMF_AVMPLUS, AMF3_OBJECT, 0x0b, 0x01,         0x03, 'a',  AMF3_OBJECT, 0x0b,
		0x01,        0x03,        'a',  AMF3_INTEGER, 0x00, 0x01, 0x01,
	};
	AMFObject obj;
	AMFObjectProperty *prop;
	AMFObject inner;
	int outer_count;

	assert_int_equal(AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE), (int)sizeof(doc));

	outer_count = AMF_CountProp(&obj);
	prop = AMF_GetProp(&obj, NULL, 0);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);
	AMFProp_GetObject(prop, &inner);

	/* the empty-name terminator is a list delimiter, not a property: the
	 * outer object holds exactly the one member wrapper, and the inner
	 * object holds exactly the one sealed member rather than the
	 * terminator as well. On master the inner object carries a second,
	 * empty-named property. Read before the reset frees them. */
	assert_int_equal(outer_count, 1);
	assert_int_equal(AMF_CountProp(&inner), 1);
	prop = AMF_GetProp(&inner, NULL, 0);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(prop->p_name.av_len, 1);
	assert_int_equal(prop->p_name.av_val[0], 'a');

	AMF_Reset(&obj);
}

/* The member type consumes the nested object's only marker (#288). */
static void nested_object_single_marker(void **state)
{
	(void)state;
	const char doc[] = {
		AMF_AVMPLUS, AMF3_OBJECT, 0x0b, 0x01,         0x03, 'a',  AMF3_OBJECT, 0x0b,
		0x01,        0x03,        'a',  AMF3_INTEGER, 0x00, 0x01, 0x01,
	};
	AMFObject obj;
	int consumed = AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE);
	if (consumed != (int)sizeof(doc)) {
		AMF_Reset(&obj);
		assert_int_equal(consumed, (int)sizeof(doc));
	}

	assert_int_equal(AMF_CountProp(&obj), 1);
	AMFObjectProperty *prop = AMF_GetProp(&obj, NULL, 0);
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);
	AMFObject outer;
	AMFProp_GetObject(prop, &outer);
	assert_int_equal(AMF_CountProp(&outer), 1);
	AVal a = AVC("a");
	prop = AMF_GetProp(&outer, &a, -1);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(AMFProp_GetType(prop), AMF_OBJECT);
	AMFObject inner;
	AMFProp_GetObject(prop, &inner);
	assert_int_equal(AMF_CountProp(&inner), 1);
	prop = AMF_GetProp(&inner, &a, -1);
	assert_true(AMFProp_IsValid(prop));
	assert_int_equal(AMFProp_GetType(prop), AMF_NUMBER);
	assert_true(AMFProp_GetNumber(prop) == 0.0);
	AMF_Reset(&obj);
}

static void nested_object_double_marker_rejected(void **state)
{
	(void)state;
	const char doc[] = {
		AMF_AVMPLUS, AMF3_OBJECT, 0x0b, 0x01, 0x03,         'a',  AMF3_OBJECT, AMF3_OBJECT,
		0x0b,        0x01,        0x03, 'a',  AMF3_INTEGER, 0x00, 0x01,        0x01,
	};
	AMFObject obj;
	int consumed = AMF_Decode(&obj, doc, (int)sizeof(doc), FALSE);
	AMF_Reset(&obj);
	assert_int_equal(consumed, -1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(inner_property_lookup),
		cmocka_unit_test(flat_dynamic_object_terminator),
		cmocka_unit_test(nested_dynamic_object),
		cmocka_unit_test(nested_object_single_marker),
		cmocka_unit_test(nested_object_double_marker_rejected),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
