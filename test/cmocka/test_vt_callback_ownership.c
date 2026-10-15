/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The complete native encoder is included; only ownership/queue operations are intercepted. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>

struct tracked_ref {
	int refs;
	unsigned retains;
	unsigned releases;
};

static struct tracked_ref input;
static struct tracked_ref sample;
static OSStatus queue_result;
static unsigned queue_calls;
static unsigned invalid_releases;
static unsigned failures;
static unsigned checks;

static struct tracked_ref *tracked(CFTypeRef ref)
{
	if (ref == &input)
		return &input;
	if (ref == &sample)
		return &sample;
	return NULL;
}

static CFTypeRef counted_retain(CFTypeRef ref)
{
	struct tracked_ref *item = tracked(ref);
	if (!item)
		return CFRetain(ref);
	item->refs++;
	item->retains++;
	return ref;
}

static void counted_release(CFTypeRef ref)
{
	if (!ref) {
		invalid_releases++;
		return;
	}
	struct tracked_ref *item = tracked(ref);
	if (!item) {
		CFRelease(ref);
		return;
	}
	if (item->refs <= 0)
		invalid_releases++;
	item->refs--;
	item->releases++;
}

static OSStatus controlled_enqueue(CMSimpleQueueRef queue, const void *element)
{
	(void)queue;
	queue_calls++;
	if (element != &sample)
		failures++;
	return queue_result;
}

#define CFRetain counted_retain
#define CFRelease counted_release
#define CMSimpleQueueEnqueue controlled_enqueue
#include "../../plugins/mac-videotoolbox/encoder.c"
#undef CMSimpleQueueEnqueue
#undef CFRelease
#undef CFRetain

static void check(bool passed, const char *test, const char *assertion)
{
	checks++;
	if (!passed) {
		failures++;
		fprintf(stderr, "%s: %s\n", test, assertion);
	}
}

static void reset(OSStatus status)
{
	input = (struct tracked_ref){.refs = 1};
	sample = (struct tracked_ref){.refs = 1};
	queue_result = status;
	queue_calls = 0;
	invalid_releases = 0;
}

static void failed_callback_releases_source(void)
{
	const char *test = "failed_callback_releases_source";
	reset(noErr);
	sample_encoded_callback(NULL, &input, -1, 0, (CMSampleBufferRef)&sample);
	check(input.refs == 0 && input.releases == 1, test, "error callback leaked its source-frame reference");
	check(sample.refs == 1 && sample.retains == 0 && sample.releases == 0, test,
	      "error callback changed the borrowed output sample");
	check(queue_calls == 0 && invalid_releases == 0, test, "error callback enqueued or over-released a reference");
}

static void full_queue_releases_retained_sample(void)
{
	const char *test = "full_queue_releases_retained_sample";
	reset(kCMSimpleQueueError_QueueIsFull);
	sample_encoded_callback(NULL, &input, noErr, 0, (CMSampleBufferRef)&sample);
	check(input.refs == 0 && input.releases == 1, test, "full queue leaked the source-frame reference");
	check(sample.refs == 1 && sample.retains == 1 && sample.releases == 1, test,
	      "failed enqueue leaked the retained output sample");
	check(queue_calls == 1 && invalid_releases == 0, test, "failed enqueue repeated or over-released a reference");
}

static void successful_enqueue_transfers_one_reference(void)
{
	const char *test = "successful_enqueue_transfers_one_reference";
	reset(noErr);
	sample_encoded_callback(NULL, &input, noErr, 0, (CMSampleBufferRef)&sample);
	check(input.refs == 0 && input.releases == 1, test, "successful callback leaked the input reference");
	check(sample.refs == 2 && sample.retains == 1 && sample.releases == 0, test,
	      "successful enqueue did not preserve the queue's owned reference");
	check(queue_calls == 1 && invalid_releases == 0, test, "successful enqueue repeated or over-released");
}

static void dropped_output_still_releases_source(void)
{
	const char *test = "dropped_output_still_releases_source";
	reset(noErr);
	sample_encoded_callback(NULL, &input, noErr, kVTEncodeInfo_FrameDropped, NULL);
	check(input.refs == 0 && input.releases == 1, test, "dropped frame leaked the input reference");
	check(queue_calls == 0 && invalid_releases == 0, test, "dropped output enqueued or over-released");
}

static void absent_source_is_not_released(void)
{
	const char *test = "absent_source_is_not_released";
	reset(noErr);
	sample_encoded_callback(NULL, NULL, noErr, 0, NULL);
	check(invalid_releases == 0 && queue_calls == 0, test, "callback attempted to release a NULL source");
}

int main(void)
{
	failed_callback_releases_source();
	full_queue_releases_retained_sample();
	successful_enqueue_transfers_one_reference();
	dropped_output_still_releases_source();
	absent_source_is_not_released();
	printf("videotoolbox callback ownership: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
