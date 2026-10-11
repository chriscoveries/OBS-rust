#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <obs.h>
#include <obs-hevc.h>
#include <obs-nal.h>

/* Characterization of the exported functions in obs-hevc.c. An HEVC NAL
 * header is two bytes; the unit type is bits 6..1 of the first, so a unit of
 * type t (layer 0) starts with t << 1. */

#define HDR(type) (uint8_t)((type) << 1), 0x01

static void test_hevc_keyframe(void **state)
{
	UNUSED_PARAMETER(state);

	const uint8_t idr[] = {0, 0, 0, 1, HDR(OBS_HEVC_NAL_VPS), 0xaa, 0, 0, 1, HDR(OBS_HEVC_NAL_IDR_W_RADL), 0xbb};
	const uint8_t cra[] = {0, 0, 1, HDR(OBS_HEVC_NAL_CRA_NUT), 0xbb};
	const uint8_t irap23[] = {0, 0, 1, HDR(OBS_HEVC_NAL_RSV_IRAP_VCL23), 0xbb};
	const uint8_t trail[] = {0, 0, 1, HDR(OBS_HEVC_NAL_TRAIL_R), 0xbb, 0, 0, 1, HDR(OBS_HEVC_NAL_IDR_N_LP), 0xcc};
	const uint8_t vps_only[] = {0, 0, 1, HDR(OBS_HEVC_NAL_VPS), 0xaa};
	/* reserved VCL types above 23 are skipped, not treated as slices */
	const uint8_t rsv24[] = {0, 0, 1, HDR(OBS_HEVC_NAL_RSV_VCL24), 0xbb, 0, 0, 1, HDR(OBS_HEVC_NAL_BLA_W_LP), 0xcc};

	assert_true(obs_hevc_keyframe(idr, sizeof(idr)));
	assert_true(obs_hevc_keyframe(cra, sizeof(cra)));
	assert_true(obs_hevc_keyframe(irap23, sizeof(irap23)));
	assert_false(obs_hevc_keyframe(trail, sizeof(trail)));
	assert_false(obs_hevc_keyframe(vps_only, sizeof(vps_only)));
	assert_true(obs_hevc_keyframe(rsv24, sizeof(rsv24)));
}

/* obsproject/obs-studio#13316: an empty packet (NULL data) is not a keyframe
 * and must not be read. */
static void test_hevc_keyframe_empty(void **state)
{
	UNUSED_PARAMETER(state);

	if (obs_hevc_keyframe(NULL, 0)) {
		fprintf(stderr, "NULL packet reported as a keyframe\n");
		fail();
	}
}

static void test_hevc_parse_packet(void **state)
{
	UNUSED_PARAMETER(state);

	uint8_t data[] = {0, 0, 0, 1, HDR(OBS_HEVC_NAL_TRAIL_R), 0xaa, 0, 0, 0, 1, HDR(OBS_HEVC_NAL_IDR_W_RADL), 0x88};
	const uint8_t hvcc[] = {0, 0, 0, 3, HDR(OBS_HEVC_NAL_TRAIL_R),    0xaa,
				0, 0, 0, 3, HDR(OBS_HEVC_NAL_IDR_W_RADL), 0x88};
	struct encoder_packet src = {0};
	struct encoder_packet out = {0};

	src.data = data;
	src.size = sizeof(data);
	src.pts = 7;
	src.keyframe = false;
	src.priority = 0;

	obs_parse_hevc_packet(&out, &src);

	assert_int_equal(out.size, sizeof(hvcc));
	assert_memory_equal(out.data, hvcc, sizeof(hvcc));
	/* an IRAP unit makes a keyframe with the highest priority */
	assert_true(out.keyframe);
	assert_int_equal(out.priority, OBS_NAL_PRIORITY_HIGHEST);
	assert_int_equal(out.drop_priority, OBS_NAL_PRIORITY_HIGHEST);
	assert_int_equal(out.pts, 7);

	long ref;
	memcpy(&ref, out.data - sizeof(long), sizeof(long));
	assert_int_equal(ref, 1);
	bfree(out.data - sizeof(long));
}

static void test_hevc_packet_priority(void **state)
{
	UNUSED_PARAMETER(state);

	uint8_t trail_n[] = {0, 0, 1, HDR(OBS_HEVC_NAL_TRAIL_N), 0xaa};
	uint8_t trail_r[] = {0, 0, 1, HDR(OBS_HEVC_NAL_TRAIL_N), 0xaa, 0, 0, 1, HDR(OBS_HEVC_NAL_RASL_R), 0xbb};
	struct encoder_packet packet = {0};

	packet.data = trail_n;
	packet.size = sizeof(trail_n);
	assert_int_equal(obs_parse_hevc_packet_priority(&packet), OBS_NAL_PRIORITY_DISPOSABLE);

	packet.data = trail_r;
	packet.size = sizeof(trail_r);
	assert_int_equal(obs_parse_hevc_packet_priority(&packet), OBS_NAL_PRIORITY_HIGH);

	/* the packet's own priority is a floor */
	packet.priority = 5;
	assert_int_equal(obs_parse_hevc_packet_priority(&packet), 5);
}

static void test_hevc_extract_headers(void **state)
{
	UNUSED_PARAMETER(state);

	const uint8_t data[] = {
		0, 0, 0, 1, HDR(OBS_HEVC_NAL_VPS),        1, 0, 0, 0, 1, HDR(OBS_HEVC_NAL_SPS),        2,
		0, 0, 0, 1, HDR(OBS_HEVC_NAL_SEI_PREFIX), 3, 0, 0, 0, 1, HDR(OBS_HEVC_NAL_PPS),        4,
		0, 0, 0, 1, HDR(OBS_HEVC_NAL_IDR_W_RADL), 5, 0, 0, 0, 1, HDR(OBS_HEVC_NAL_SEI_SUFFIX), 6};
	const size_t unit = 7; /* every unit above is a 4-byte code plus 3 bytes */

	uint8_t *packet, *header, *sei;
	size_t packet_size, header_size, sei_size;
	obs_extract_hevc_headers(data, sizeof(data), &packet, &packet_size, &header, &header_size, &sei, &sei_size);

	/* VPS, SPS and PPS are headers; both SEI kinds are SEI */
	assert_int_equal(header_size, 3 * unit);
	assert_memory_equal(header, data, 2 * unit);
	assert_memory_equal(header + 2 * unit, data + 3 * unit, unit);
	assert_int_equal(sei_size, 2 * unit);
	assert_memory_equal(sei, data + 2 * unit, unit);
	assert_memory_equal(sei + unit, data + 5 * unit, unit);
	assert_int_equal(packet_size, unit);
	assert_memory_equal(packet, data + 4 * unit, unit);

	bfree(packet);
	bfree(header);
	bfree(sei);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_hevc_keyframe),        cmocka_unit_test(test_hevc_keyframe_empty),
		cmocka_unit_test(test_hevc_parse_packet),    cmocka_unit_test(test_hevc_packet_priority),
		cmocka_unit_test(test_hevc_extract_headers),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
