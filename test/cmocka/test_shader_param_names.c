#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <graphics/shader-parser.h>

/*
 * Characterization tests for libobs/graphics/shader-param-names.c
 * (obs-rust/obs-studio#839): the shader-parser's string-to-enum lookups.
 * They pin CURRENT behavior. Cases marked "characterized, not endorsed" are
 * quirks a port must reproduce.
 */

static void test_shader_param_type(void **state)
{
	UNUSED_PARAMETER(state);

	assert_int_equal(get_shader_param_type("float"), GS_SHADER_PARAM_FLOAT);
	assert_int_equal(get_shader_param_type("float2"), GS_SHADER_PARAM_VEC2);
	assert_int_equal(get_shader_param_type("float3"), GS_SHADER_PARAM_VEC3);
	assert_int_equal(get_shader_param_type("float4"), GS_SHADER_PARAM_VEC4);
	assert_int_equal(get_shader_param_type("int2"), GS_SHADER_PARAM_INT2);
	assert_int_equal(get_shader_param_type("int3"), GS_SHADER_PARAM_INT3);
	assert_int_equal(get_shader_param_type("int4"), GS_SHADER_PARAM_INT4);
	assert_int_equal(get_shader_param_type("float4x4"), GS_SHADER_PARAM_MATRIX4X4);
	assert_int_equal(get_shader_param_type("bool"), GS_SHADER_PARAM_BOOL);
	assert_int_equal(get_shader_param_type("int"), GS_SHADER_PARAM_INT);
	assert_int_equal(get_shader_param_type("string"), GS_SHADER_PARAM_STRING);

	assert_int_equal(get_shader_param_type("texture2d"), GS_SHADER_PARAM_TEXTURE);
	assert_int_equal(get_shader_param_type("texture3d"), GS_SHADER_PARAM_TEXTURE);
	assert_int_equal(get_shader_param_type("texture_rect"), GS_SHADER_PARAM_TEXTURE);
	/* characterized, not endorsed: anything that starts with "texture" is a
	 * texture, also "texture" itself and "textures" */
	assert_int_equal(get_shader_param_type("texture"), GS_SHADER_PARAM_TEXTURE);
	assert_int_equal(get_shader_param_type("textures"), GS_SHADER_PARAM_TEXTURE);

	/* exact, case-sensitive matches only */
	assert_int_equal(get_shader_param_type("Float"), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type("float5"), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type("float4x3"), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type("Texture2d"), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type("textur"), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type(""), GS_SHADER_PARAM_UNKNOWN);
	assert_int_equal(get_shader_param_type("half"), GS_SHADER_PARAM_UNKNOWN);
}

static void test_sample_filter(void **state)
{
	UNUSED_PARAMETER(state);

	/* the short names match case-insensitively */
	assert_int_equal(get_sample_filter("Anisotropy"), GS_FILTER_ANISOTROPIC);
	assert_int_equal(get_sample_filter("anisotropy"), GS_FILTER_ANISOTROPIC);
	assert_int_equal(get_sample_filter("Point"), GS_FILTER_POINT);
	assert_int_equal(get_sample_filter("POINT"), GS_FILTER_POINT);
	assert_int_equal(get_sample_filter("Linear"), GS_FILTER_LINEAR);
	assert_int_equal(get_sample_filter("linear"), GS_FILTER_LINEAR);

	/* the D3D names match exactly */
	assert_int_equal(get_sample_filter("MIN_MAG_MIP_POINT"), GS_FILTER_POINT);
	assert_int_equal(get_sample_filter("MIN_MAG_MIP_LINEAR"), GS_FILTER_LINEAR);
	assert_int_equal(get_sample_filter("MIN_MAG_POINT_MIP_LINEAR"), GS_FILTER_MIN_MAG_POINT_MIP_LINEAR);
	assert_int_equal(get_sample_filter("MIN_POINT_MAG_LINEAR_MIP_POINT"), GS_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT);
	assert_int_equal(get_sample_filter("MIN_POINT_MAG_MIP_LINEAR"), GS_FILTER_MIN_POINT_MAG_MIP_LINEAR);
	assert_int_equal(get_sample_filter("MIN_LINEAR_MAG_MIP_POINT"), GS_FILTER_MIN_LINEAR_MAG_MIP_POINT);
	assert_int_equal(get_sample_filter("MIN_LINEAR_MAG_POINT_MIP_LINEAR"),
			 GS_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR);
	assert_int_equal(get_sample_filter("MIN_MAG_LINEAR_MIP_POINT"), GS_FILTER_MIN_MAG_LINEAR_MIP_POINT);

	/* characterized, not endorsed: a D3D name in another case, and any
	 * unknown name, fall back to linear */
	assert_int_equal(get_sample_filter("min_mag_mip_point"), GS_FILTER_LINEAR);
	assert_int_equal(get_sample_filter("min_mag_point_mip_linear"), GS_FILTER_LINEAR);
	assert_int_equal(get_sample_filter("Bilinear"), GS_FILTER_LINEAR);
	assert_int_equal(get_sample_filter(""), GS_FILTER_LINEAR);
}

static void test_address_mode(void **state)
{
	UNUSED_PARAMETER(state);

	/* all case-insensitive */
	assert_int_equal(get_address_mode("Wrap"), GS_ADDRESS_WRAP);
	assert_int_equal(get_address_mode("REPEAT"), GS_ADDRESS_WRAP);
	assert_int_equal(get_address_mode("clamp"), GS_ADDRESS_CLAMP);
	assert_int_equal(get_address_mode("None"), GS_ADDRESS_CLAMP);
	assert_int_equal(get_address_mode("Mirror"), GS_ADDRESS_MIRROR);
	assert_int_equal(get_address_mode("border"), GS_ADDRESS_BORDER);
	assert_int_equal(get_address_mode("MirrorOnce"), GS_ADDRESS_MIRRORONCE);
	assert_int_equal(get_address_mode("MIRRORONCE"), GS_ADDRESS_MIRRORONCE);

	/* unknown names fall back to clamp; whole-string matches only */
	assert_int_equal(get_address_mode("Mirror Once"), GS_ADDRESS_CLAMP);
	assert_int_equal(get_address_mode("Wrapped"), GS_ADDRESS_CLAMP);
	assert_int_equal(get_address_mode(""), GS_ADDRESS_CLAMP);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_shader_param_type),
		cmocka_unit_test(test_sample_filter),
		cmocka_unit_test(test_address_mode),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
