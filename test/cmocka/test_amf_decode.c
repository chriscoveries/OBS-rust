#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include <librtmp/amf.h>
#include <librtmp/rtmp.h>

static void decode_int16_bytes(void **state)
{
	(void)state;
	const uint8_t byte_values[] = {0x80, 0xff};

	assert_int_equal(AMF_DecodeInt16("\x00\x00"), 0x0000);
	assert_int_equal(AMF_DecodeInt16("\x7f\xff"), 0x7fff);
	assert_int_equal(AMF_DecodeInt16("\x80\x00"), 0x8000);
	assert_int_equal(AMF_DecodeInt16("\xff\xff"), 0xffff);
	assert_int_equal(AMF_DecodeInt16("\x12\x34"), 0x1234);

	for (size_t byte = 0; byte < sizeof(byte_values); byte++) {
		for (size_t position = 0; position < 2; position++) {
			char data[2] = {0};
			data[position] = (char)byte_values[byte];
			assert_int_equal(AMF_DecodeInt16(data), (uint16_t)byte_values[byte] << ((1 - position) * 8));
		}
	}
}

static void decode_int24_bytes(void **state)
{
	(void)state;
	const uint8_t byte_values[] = {0x80, 0xff};

	assert_int_equal(AMF_DecodeInt24("\x00\x00\x00"), 0x000000);
	assert_int_equal(AMF_DecodeInt24("\x7f\xff\xff"), 0x7fffff);
	assert_int_equal(AMF_DecodeInt24("\x80\x00\x00"), 0x800000);
	assert_int_equal(AMF_DecodeInt24("\xff\xff\xff"), 0xffffff);
	assert_int_equal(AMF_DecodeInt24("\x12\x34\x56"), 0x123456);

	for (size_t byte = 0; byte < sizeof(byte_values); byte++) {
		for (size_t position = 0; position < 3; position++) {
			char data[3] = {0};
			data[position] = (char)byte_values[byte];
			assert_int_equal(AMF_DecodeInt24(data), (uint32_t)byte_values[byte] << ((2 - position) * 8));
		}
	}
}

static void decode_int32_bytes(void **state)
{
	(void)state;
	const uint8_t byte_values[] = {0x80, 0xff};
	const struct {
		char data[4];
		uint32_t expected;
	} cases[] = {
		{{0x00, 0x00, 0x00, 0x00}, UINT32_C(0x00000000)},
		{{0x7f, (char)0xff, (char)0xff, (char)0xff}, UINT32_C(0x7fffffff)},
		{{(char)0x80, 0x00, 0x00, 0x00}, UINT32_C(0x80000000)},
		{{(char)0xff, (char)0xff, (char)0xff, (char)0xff}, UINT32_C(0xffffffff)},
		{{0x12, 0x34, 0x56, 0x78}, UINT32_C(0x12345678)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		assert_int_equal(AMF_DecodeInt32(cases[i].data), cases[i].expected);

	for (size_t byte = 0; byte < sizeof(byte_values); byte++) {
		for (size_t position = 0; position < 4; position++) {
			char data[4] = {0};
			data[position] = (char)byte_values[byte];
			assert_int_equal(AMF_DecodeInt32(data), (uint32_t)byte_values[byte] << ((3 - position) * 8));
		}
	}
}

static void assert_decode_int32le(const char data[4], uint32_t expected)
{
	RTMP *rtmp = RTMP_Alloc();
	assert_non_null(rtmp);
	RTMP_Init(rtmp);

	char header[] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, data[0], data[1], data[2], data[3]};
	memcpy(rtmp->m_sb.sb_buf, header, sizeof(header));
	rtmp->m_sb.sb_start = rtmp->m_sb.sb_buf;
	rtmp->m_sb.sb_size = sizeof(header);

	RTMPPacket packet = {0};
	assert_true(RTMP_ReadPacket(rtmp, &packet));
	assert_int_equal((uint32_t)packet.m_nInfoField2, expected);

	RTMP_Close(rtmp);
	RTMP_Free(rtmp);
}

static void decode_int32le_bytes(void **state)
{
	(void)state;
	const uint8_t byte_values[] = {0x80, 0xff};
	const struct {
		char data[4];
		uint32_t expected;
	} cases[] = {
		{{0x00, 0x00, 0x00, 0x00}, UINT32_C(0x00000000)},
		{{(char)0xff, (char)0xff, (char)0xff, 0x7f}, UINT32_C(0x7fffffff)},
		{{0x00, 0x00, 0x00, (char)0x80}, UINT32_C(0x80000000)},
		{{(char)0xff, (char)0xff, (char)0xff, (char)0xff}, UINT32_C(0xffffffff)},
		{{0x78, 0x56, 0x34, 0x12}, UINT32_C(0x12345678)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		assert_decode_int32le(cases[i].data, cases[i].expected);

	for (size_t byte = 0; byte < sizeof(byte_values); byte++) {
		for (size_t position = 0; position < 4; position++) {
			char data[4] = {0};
			data[position] = (char)byte_values[byte];
			assert_decode_int32le(data, (uint32_t)byte_values[byte] << (position * 8));
		}
	}
}

static void client_packet_decodes_flv_timestamp(void **state)
{
	(void)state;
	char body[] = {
		0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (char)0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b,
	};
	RTMP *rtmp = RTMP_Alloc();
	assert_non_null(rtmp);
	RTMP_Init(rtmp);

	RTMPPacket packet = {0};
	packet.m_packetType = RTMP_PACKET_TYPE_FLASH_VIDEO;
	packet.m_nBodySize = sizeof(body);
	packet.m_body = body;
	assert_int_equal(RTMP_ClientPacket(rtmp, &packet), 1);
	assert_int_equal(rtmp->m_mediaStamp, UINT32_C(0x80000000));

	RTMP_Close(rtmp);
	RTMP_Free(rtmp);
}

static void read_decodes_flv_timestamps(void **state)
{
	(void)state;
	const char packet[] = {
		0x03, 0x00, 0x00, 0x00, 0x00, 0x00,       0x0f, 0x16, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, (char)0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b,
	};
	char output[sizeof(packet)] = {0};
	char initial_frame = 0;
	RTMP *rtmp = RTMP_Alloc();
	assert_non_null(rtmp);
	RTMP_Init(rtmp);
	rtmp->m_sb.sb_socket = 0;
	memcpy(rtmp->m_sb.sb_buf, packet, sizeof(packet));
	rtmp->m_sb.sb_start = rtmp->m_sb.sb_buf;
	rtmp->m_sb.sb_size = sizeof(packet);
	rtmp->m_read.flags = RTMP_READ_HEADER | RTMP_READ_RESUME | RTMP_READ_GOTFLVK;
	rtmp->m_read.initialFrameType = RTMP_PACKET_TYPE_VIDEO;
	rtmp->m_read.initialFrame = &initial_frame;
	rtmp->m_read.nInitialFrameSize = 1;
	rtmp->m_read.nResumeTS = UINT32_C(0x7fffffff);

	assert_int_equal(RTMP_Read(rtmp, output, sizeof(output)), 15);
	assert_int_equal(rtmp->m_mediaStamp, UINT32_C(0x80000000));
	assert_int_equal(rtmp->m_read.timestamp, UINT32_C(0x7fffffff));
	assert_memory_equal(output + 4, "\xff\xff\xff\x7f", 4);

	rtmp->m_sb.sb_socket = -1;
	RTMP_Close(rtmp);
	RTMP_Free(rtmp);
}

static void write_decodes_flv_timestamp(void **state)
{
	(void)state;
	const char tag[] = {
		0x09, 0x00, 0x00, 0x02, 0x12, 0x34, 0x56, (char)0x80, 0x00, 0x00, 0x00, 0x00,
	};
	RTMP *rtmp = RTMP_Alloc();
	assert_non_null(rtmp);
	RTMP_Init(rtmp);

	assert_int_equal(RTMP_Write(rtmp, tag, sizeof(tag), 0), sizeof(tag));
	assert_int_equal(rtmp->m_write.m_nTimeStamp, UINT32_C(0x80123456));

	RTMP_Close(rtmp);
	RTMP_Free(rtmp);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(decode_int16_bytes),
		cmocka_unit_test(decode_int24_bytes),
		cmocka_unit_test(decode_int32le_bytes),
		cmocka_unit_test(decode_int32_bytes),
		cmocka_unit_test(client_packet_decodes_flv_timestamp),
		cmocka_unit_test(read_decodes_flv_timestamps),
		cmocka_unit_test(write_decodes_flv_timestamp),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
