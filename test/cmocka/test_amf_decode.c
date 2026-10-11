#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
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

static int discard_rtmp_output(RTMPSockBuf *sockbuf, const char *buffer, int length, void *context)
{
	(void)sockbuf;
	(void)buffer;
	(*(int *)context)++;
	return length;
}

static void secure_token_decode_uses_unsigned_shift(void **state)
{
	(void)state;
	AVal result = AVC("_result");
	AVal secure_token = AVC("secureToken");
	char response_value[] = "00000080";
	AVal response = {response_value, sizeof(response_value) - 1};
	char body[128];
	char *end = body;
	int send_count = 0;

	end = AMF_EncodeString(end, body + sizeof(body), &result);
	end = AMF_EncodeNumber(end, body + sizeof(body), 1.0);
	*end++ = AMF_NULL;
	*end++ = AMF_OBJECT;
	end = AMF_EncodeNamedString(end, body + sizeof(body), &secure_token, &response);
	*end++ = 0;
	*end++ = 0;
	*end++ = AMF_OBJECT_END;

	RTMP *rtmp = RTMP_Alloc();
	assert_non_null(rtmp);
	RTMP_Init(rtmp);
	rtmp->Link.token = (AVal)AVC("0123456789abcdef");
	rtmp->Link.protocol |= RTMP_FEATURE_WRITE;
	rtmp->m_methodCalls = calloc(1, sizeof(*rtmp->m_methodCalls));
	assert_non_null(rtmp->m_methodCalls);
	rtmp->m_methodCalls[0].name.av_val = malloc(sizeof("connect"));
	assert_non_null(rtmp->m_methodCalls[0].name.av_val);
	memcpy(rtmp->m_methodCalls[0].name.av_val, "connect", sizeof("connect"));
	rtmp->m_methodCalls[0].name.av_len = sizeof("connect") - 1;
	rtmp->m_methodCalls[0].num = 1;
	rtmp->m_numCalls = 1;
	rtmp->m_bCustomSend = TRUE;
	rtmp->m_customSendFunc = discard_rtmp_output;
	rtmp->m_customSendParam = &send_count;

	RTMPPacket packet = {0};
	packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
	packet.m_nBodySize = (uint32_t)(end - body);
	packet.m_body = body;
	RTMP_ClientPacket(rtmp, &packet);
	assert_int_equal(send_count, 1);

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
		cmocka_unit_test(secure_token_decode_uses_unsigned_shift),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
