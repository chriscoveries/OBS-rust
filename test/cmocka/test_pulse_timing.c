#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/c99defs.h>
#include <util/util_uint64.h>

#include "pulse-timing.h"

/* Microphones captured through PulseAudio (pipewire-pulse) drop out for
 * ~75 ms at a time, logged as "Audio timestamp ... exceeded
 * TS_SMOOTHING_THRESHOLD, diff=7xxxxxxx ns" (obsproject/obs-studio#12835),
 * and two sources on one device sound choppy (#10808).
 *
 * Record streams deliver audio in bursts: pipewire-pulse hands over all
 * fragments of a graph cycle at once when the quantum is longer than the
 * fragment, a descheduled capture thread reads the fragments that piled up
 * back to back, and a second stream on OBS's one connection gets its packets
 * up to 150 ms at a time. Stamping each packet with its arrival time gives
 * every packet of a burst the same timestamp, so once a burst spans libobs'
 * 70 ms smoothing window libobs places a packet ~75 ms in the past
 * (overwriting audio) and the next one ~75 ms in the future (silence).
 *
 * The data itself is contiguous, so the timestamps must be too. */

#define RATE 48000
#define FRAG 1200 /* the 25 ms fragsize pulse-input asks for */
#define USEC 1000ULL
#define MSEC 1000000ULL
#define SEC 1000000000ULL
#define FRAG_NS (25 * MSEC)
#define START_NS (1000 * SEC)

/* libobs/obs-source.c */
#define TS_SMOOTHING_THRESHOLD 70000000ULL
/* pulse-input.c drops what it reads in the first 500 ms of a stream. */
#define STARTUP_NS (500 * MSEC)

static uint64_t frames_to_ns(uint64_t frames)
{
	return util_mul_div64(frames, SEC, RATE);
}

static uint64_t ts_diff(uint64_t a, uint64_t b)
{
	return a > b ? a - b : b - a;
}

struct replay {
	struct pulse_timing timing;
	uint64_t first_read;
	/* What source_output_audio_data expects next; 0 before the first. */
	uint64_t expected;
	unsigned breaks;
	uint64_t worst_break;
	unsigned packets;
	/* Each packet must end by the time it is read, and not long before. */
	unsigned future_stamps;
	uint64_t max_lag;
};

/* Feed one packet read at now_ns and replay libobs' first-stage audio
 * timestamp smoothing on the result (source_output_audio_data, direct
 * timestamps): within the threshold a packet is moved to where the previous
 * one ended, beyond it the packet is placed at its own timestamp. Returns
 * the packet's timestamp. */
static uint64_t read_packet(struct replay *r, uint64_t now_ns, size_t frames)
{
	uint64_t ts = pulse_timing_packet_ts(&r->timing, now_ns, frames, RATE);
	uint64_t placed = ts;
	uint64_t end = ts + frames_to_ns(frames);

	/* Audio cannot have been captured after it was read. */
	if (end > now_ns)
		r->future_stamps++;
	else if (now_ns - end > r->max_lag)
		r->max_lag = now_ns - end;

	if (!r->first_read)
		r->first_read = now_ns;
	if (now_ns - r->first_read <= STARTUP_NS)
		return ts;

	if (r->expected) {
		uint64_t diff = ts_diff(placed, r->expected);

		if (diff < TS_SMOOTHING_THRESHOLD) {
			placed = r->expected;
		} else {
			r->breaks++;
			if (diff > r->worst_break)
				r->worst_break = diff;
		}
	}
	r->expected = placed + frames_to_ns(frames);
	r->packets++;
	return ts;
}

static void assert_seamless(const struct replay *r)
{
	if (r->breaks)
		print_error("%u of %u packets broke smoothing, worst by %u ms\n", r->breaks, r->packets,
			    (unsigned)(r->worst_break / MSEC));
	assert_int_equal(r->breaks, 0);
	assert_int_equal(r->future_stamps, 0);
}

/* A server that hands over every fragment as soon as it is captured, the
 * device clock running @ppm faster than the system clock (negative: slower).
 * Returns the time of the last packet. */
static uint64_t steady(struct replay *r, uint64_t start_ns, unsigned secs, int ppm)
{
	uint64_t period = FRAG_NS * (1000000 - ppm) / 1000000;
	uint64_t n = (uint64_t)secs * SEC / period;
	uint64_t now = start_ns;

	for (uint64_t i = 1; i <= n; i++) {
		now = start_ns + i * period;
		read_packet(r, now, FRAG);
	}
	return now;
}

static void evenly_delivered_packets_are_contiguous(void **state)
{
	UNUSED_PARAMETER(state);
	struct replay r = {0};
	uint64_t prev_ts = 0;

	for (uint64_t i = 0; i < 400; i++) {
		uint64_t ts = read_packet(&r, START_NS + i * FRAG_NS, FRAG);

		if (i > 0)
			assert_true(ts_diff(ts - prev_ts, FRAG_NS) < MSEC);
		prev_ts = ts;
	}
	assert_seamless(&r);
	assert_true(r.max_lag < 100 * MSEC);
}

/* Six 25 ms packets delivered at once every 150 ms: the pattern measured
 * for the second of two streams on one connection (#10808). */
static void burst_delivery_stays_contiguous(void **state)
{
	UNUSED_PARAMETER(state);
	struct replay r = {0};
	uint64_t prev_ts = 0;

	/* A few evenly delivered packets first, as when the stream starts. */
	for (uint64_t i = 0; i < 8; i++)
		prev_ts = read_packet(&r, START_NS + i * FRAG_NS, FRAG);

	uint64_t burst_at = START_NS + 7 * FRAG_NS;
	for (int burst = 0; burst < 40; burst++) {
		burst_at += 6 * FRAG_NS;
		for (uint64_t i = 0; i < 6; i++) {
			uint64_t ts = read_packet(&r, burst_at + i * 10 * USEC, FRAG);

			assert_true(ts_diff(ts - prev_ts, FRAG_NS) < 2 * MSEC);
			prev_ts = ts;
		}
	}
	assert_seamless(&r);
}

/* pipewire-pulse with a graph quantum of 8192 (170 ms, e.g. a virtual or
 * remapped source in a graph some other client drives with a large
 * latency): each cycle the 25 ms fragments it completed arrive together.
 * Measured on a private PipeWire 1.6.8: 75 ms breaks for 239 of 580
 * packets in 14 s. */
static void server_cycle_bursts_are_seamless(void **state)
{
	UNUSED_PARAMETER(state);
	const uint64_t quantum = 8192;
	struct replay r = {0};
	uint64_t sent = 0;

	for (uint64_t cycle = 1; cycle <= 60; cycle++) {
		uint64_t now = START_NS + frames_to_ns(cycle * quantum);

		for (; (sent + 1) * FRAG <= cycle * quantum; sent++)
			read_packet(&r, now, FRAG);
	}
	assert_seamless(&r);
	assert_true(r.max_lag < 500 * MSEC);
}

/* The default quantum, but the capture thread is held up for 100 ms (OBS
 * under encoding load) and then reads the fragments that queued up back to
 * back. Measured: two ~75 ms breaks per stall. */
static void capture_thread_stall_is_seamless(void **state)
{
	UNUSED_PARAMETER(state);
	const uint64_t stall_end = START_NS + 2 * SEC + 100 * MSEC;
	struct replay r = {0};

	for (uint64_t i = 1; i <= 200; i++) {
		uint64_t captured = START_NS + i * FRAG_NS;

		/* Fragments completed during the stall are read when it ends. */
		if (captured > START_NS + 2 * SEC && captured < stall_end)
			read_packet(&r, stall_end, FRAG);
		else
			read_packet(&r, captured, FRAG);
	}
	assert_seamless(&r);
	assert_true(r.max_lag < 500 * MSEC);
}

/* Arrival times in us of 120 consecutive 1200-frame packets of the second
 * of two OBS sources on one PipeWire 1.6.8 virtual source (3 s of audio). */
static const uint64_t recorded_arrivals_us[] = {
	0,       63941,   63948,   63955,   106557,  255915,  255926,  255933,  255939,  255945,  255951,  383924,
	383934,  383941,  383946,  383952,  533281,  533293,  533298,  533304,  533309,  533313,  682562,  682570,
	682575,  682580,  682584,  682589,  746615,  746624,  746633,  831904,  831914,  831921,  981242,  981253,
	981260,  981267,  981273,  981279,  1109243, 1109252, 1109260, 1109266, 1109273, 1258603, 1258615, 1258622,
	1258629, 1258636, 1258642, 1407913, 1407927, 1407933, 1407938, 1407943, 1407948, 1493221, 1493226, 1493231,
	1493236, 1557249, 1557260, 1706576, 1706590, 1706600, 1706611, 1706620, 1706630, 1770566, 1770574, 1770590,
	1834563, 1834570, 1834582, 1919898, 1919903, 1919908, 1983916, 1983924, 2133306, 2133318, 2133326, 2133333,
	2133340, 2133347, 2282574, 2282585, 2282592, 2282599, 2282606, 2282612, 2389241, 2389252, 2389263, 2389273,
	2389283, 2431892, 2581235, 2581244, 2581249, 2581254, 2581259, 2581264, 2666589, 2666595, 2666601, 2666606,
	2709258, 2859090, 2859103, 2859111, 2859119, 2859125, 2859132, 2879985, 2944211, 2944219, 2944228, 3007926,
};

static void recorded_second_stream_is_not_choppy(void **state)
{
	UNUSED_PARAMETER(state);
	struct replay r = {0};

	for (size_t i = 0; i < sizeof(recorded_arrivals_us) / sizeof(recorded_arrivals_us[0]); i++)
		read_packet(&r, START_NS + recorded_arrivals_us[i] * USEC, FRAG);

	assert_seamless(&r);
}

/* Audio really lost (the stream stalled and nothing caught up): the
 * timestamps must follow the clock, not keep counting samples. */
static void lost_audio_moves_the_timeline(void **state)
{
	UNUSED_PARAMETER(state);
	struct replay r = {0};
	uint64_t now = START_NS;
	uint64_t ts = 0;

	for (int i = 0; i < 40; i++, now += FRAG_NS)
		read_packet(&r, now, FRAG);

	now += 2 * SEC;
	for (int i = 0; i < 400; i++, now += FRAG_NS)
		ts = read_packet(&r, now, FRAG);

	assert_true(ts_diff(ts + FRAG_NS, now - FRAG_NS) < TS_SMOOTHING_THRESHOLD);
	assert_int_equal(r.future_stamps, 0);
}

/* A device clock 0.1% off the system clock for 10 minutes: the timestamps
 * keep following the system clock, never ahead of it and never far behind. */
static void follow_device_clock(int ppm)
{
	struct replay r = {0};
	uint64_t last = steady(&r, START_NS, 600, ppm);
	uint64_t ts = pulse_timing_packet_ts(&r.timing, last + FRAG_NS, FRAG, RATE);

	assert_int_equal(r.future_stamps, 0);
	assert_true(r.max_lag < 500 * MSEC);
	assert_true(ts_diff(ts + FRAG_NS, last + FRAG_NS) < TS_SMOOTHING_THRESHOLD);
}

static void fast_device_clock_is_followed(void **state)
{
	UNUSED_PARAMETER(state);
	follow_device_clock(1000);
}

static void slow_device_clock_is_followed(void **state)
{
	UNUSED_PARAMETER(state);
	follow_device_clock(-1000);
}

/* After a reset (the stream was stopped) the next packet starts a new
 * timeline at its own arrival. */
static void reset_starts_a_new_timeline(void **state)
{
	UNUSED_PARAMETER(state);
	struct replay r = {0};
	uint64_t later = START_NS + 100 * SEC;

	steady(&r, START_NS, 5, 0);
	pulse_timing_reset(&r.timing);
	assert_int_equal(pulse_timing_packet_ts(&r.timing, later, FRAG, RATE), later - FRAG_NS);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(evenly_delivered_packets_are_contiguous),
		cmocka_unit_test(burst_delivery_stays_contiguous),
		cmocka_unit_test(server_cycle_bursts_are_seamless),
		cmocka_unit_test(capture_thread_stall_is_seamless),
		cmocka_unit_test(recorded_second_stream_is_not_choppy),
		cmocka_unit_test(lost_audio_moves_the_timeline),
		cmocka_unit_test(fast_device_clock_is_followed),
		cmocka_unit_test(slow_device_clock_is_followed),
		cmocka_unit_test(reset_starts_a_new_timeline),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
