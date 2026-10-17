/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real session constructor/destructor with deterministic API failures. */
#include <obs-module.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>
#include <string.h>

static unsigned session_token;
static unsigned encoder_token;
static unsigned video_token;
static unsigned copy_calls;
static unsigned set_calls;
static unsigned invalid_session_calls;
static unsigned invalidations;
static unsigned releases;
static unsigned checks;
static unsigned failures;
static OSStatus create_status;
static const OSStatus create_failure = -12902;
static const OSStatus setup_failure = -12900;

static VTCompressionSessionRef fake_session(void)
{
	return (VTCompressionSessionRef)&session_token;
}

static const char *test_codec(const obs_encoder_t *encoder)
{
	(void)encoder;
	return "h264";
}

static const char *test_encoder_name(const obs_encoder_t *encoder)
{
	(void)encoder;
	return "session failure test";
}

static video_t *test_encoder_video(const obs_encoder_t *encoder)
{
	(void)encoder;
	return (video_t *)&video_token;
}

static const struct video_output_info *test_video_info(const video_t *video)
{
	(void)video;
	static const struct video_output_info info = {
		.format = VIDEO_FORMAT_NV12,
		.fps_num = 30,
		.fps_den = 1,
		.width = 64,
		.height = 64,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
	};
	return &info;
}

static OSStatus controlled_create(CFAllocatorRef allocator, int32_t width, int32_t height, CMVideoCodecType codec,
				  CFDictionaryRef encoder_spec, CFDictionaryRef attributes,
				  CFAllocatorRef data_allocator, VTCompressionOutputCallback callback, void *context,
				  VTCompressionSessionRef *out)
{
	(void)allocator;
	(void)width;
	(void)height;
	(void)codec;
	(void)encoder_spec;
	(void)attributes;
	(void)data_allocator;
	(void)callback;
	(void)context;
	*out = create_status == noErr ? fake_session() : NULL;
	return create_status;
}

static OSStatus controlled_copy(VTSessionRef session, CFStringRef key, CFAllocatorRef allocator, void *out)
{
	(void)key;
	(void)allocator;
	++copy_calls;
	if (session != (VTSessionRef)fake_session())
		++invalid_session_calls;
	const CFTypeRef null_value = NULL;
	memcpy(out, &null_value, sizeof(null_value));
	return setup_failure;
}

static OSStatus controlled_set(VTSessionRef session, CFStringRef key, CFTypeRef value)
{
	(void)key;
	(void)value;
	++set_calls;
	if (session != (VTSessionRef)fake_session())
		++invalid_session_calls;
	return noErr;
}

static OSStatus controlled_set_all(VTSessionRef session, CFDictionaryRef values)
{
	(void)values;
	++set_calls;
	if (session != (VTSessionRef)fake_session())
		++invalid_session_calls;
	/* Fail at the first bulk property write, before colour setup or encoding. */
	return setup_failure;
}

static void controlled_invalidate(VTCompressionSessionRef session)
{
	if (session != fake_session())
		++invalid_session_calls;
	++invalidations;
}

static void controlled_release(CFTypeRef value)
{
	if (value == (CFTypeRef)fake_session()) {
		++releases;
		return;
	}
	CFRelease(value);
}

#define obs_encoder_get_codec test_codec
#define obs_encoder_get_name test_encoder_name
#define obs_encoder_video test_encoder_video
#define video_output_get_info test_video_info
#define VTCompressionSessionCreate controlled_create
#define VTSessionCopyProperty controlled_copy
#define VTSessionSetProperty controlled_set
#define VTSessionSetProperties controlled_set_all
#define VTCompressionSessionInvalidate controlled_invalidate
#define CFRelease controlled_release
#include "../../plugins/mac-videotoolbox/encoder.c"
#undef CFRelease
#undef VTCompressionSessionInvalidate
#undef VTSessionSetProperties
#undef VTSessionSetProperty
#undef VTSessionCopyProperty
#undef VTCompressionSessionCreate
#undef video_output_get_info
#undef obs_encoder_video
#undef obs_encoder_get_name
#undef obs_encoder_get_codec

static void check(bool condition, const char *name, const char *message)
{
	++checks;
	if (!condition) {
		++failures;
		fprintf(stderr, "%s: %s\n", name, message);
	}
}

static struct vt_encoder *new_encoder(OSStatus status)
{
	copy_calls = 0;
	set_calls = 0;
	invalid_session_calls = 0;
	invalidations = 0;
	releases = 0;
	create_status = status;
	struct vt_encoder *enc = bzalloc(sizeof(*enc));
	enc->encoder = (obs_encoder_t *)&encoder_token;
	enc->codec_type = kCMVideoCodecType_H264;
	enc->vt_encoder_id = "test.encoder";
	enc->width = 64;
	enc->height = 64;
	enc->fps_num = 30;
	enc->fps_den = 1;
	enc->keyint = 2;
	enc->profile = "high";
	enc->rate_control = "ABR";
	enc->bitrate = 1000;
	enc->vt_pix_fmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
	enc->colorspace = VIDEO_CS_709;
	return enc;
}

static void failed_creation_stops_before_session_use(void)
{
	const char *name = "failed_creation_stops_before_session_use";
	struct vt_encoder *enc = new_encoder(create_failure);
	const OSStatus status = create_encoder(enc);
	check(status == create_failure, name, "creation error was replaced by a later property error");
	check(copy_calls == 0 && set_calls == 0, name, "failed creation continued into session properties");
	check(enc->session == NULL && invalid_session_calls == 0, name, "failed creation used an invalid session");
	vt_destroy(enc);
	check(invalidations == 0 && releases == 0, name, "destroy attempted to own a failed session");
}

static void failed_setup_keeps_session_owned_for_cleanup(void)
{
	const char *name = "failed_setup_keeps_session_owned_for_cleanup";
	struct vt_encoder *enc = new_encoder(noErr);
	const OSStatus status = create_encoder(enc);
	check(status == setup_failure, name, "property error was not propagated");
	check(enc->session == fake_session(), name, "created session was lost before setup failure cleanup");
	check(copy_calls == 1 && set_calls == 2 && invalid_session_calls == 0, name,
	      "test did not reach the expected property failure");
	check(invalidations == 0 && releases == 0, name, "session was released before the owning destructor");
	vt_destroy(enc);
	check(invalidations == 1 && releases == 1, name, "destructor did not invalidate and release the session once");
}

int main(void)
{
	failed_creation_stops_before_session_use();
	failed_setup_keeps_session_owned_for_cleanup();
	printf("videotoolbox session failure: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
