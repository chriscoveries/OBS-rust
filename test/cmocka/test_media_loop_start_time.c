/*
 * A looping Media Source must play the file again right after its end, with
 * no silence and no frozen picture between the loops (obsproject/obs-studio
 * #12028, reported by stevemagruder: a looped .mp3 plays a dead spot before
 * every repeat, the same sound as a .wav loops without one).
 *
 * Two things add up to it. An .mp3 starts at the encoder delay (23 ms for
 * a LAME file at 48 kHz), and other files start later (files cut from a
 * stream, MPEG-TS). At the end of the file media-playback adds the end time
 * of the file to its time base, although the time stamps it sends are
 * counted from the first frame, so every loop starts late by the start time
 * of the file. And the decoder has no packet time base, so it cannot move
 * the time stamp of the first .mp3 frame it trims by the encoder delay:
 * that frame keeps the time stamp of the whole frame and the audio jumps
 * after it, on every loop.
 *
 * The cases write 1 s files (PCM audio whose first frame is at START_MS,
 * the same with MPEG-4 video, and an .mp3 where FFmpeg has an MP3 encoder),
 * play them looped through media-playback (the code behind ffmpeg_source)
 * and check that the time stamps of the audio and the video run on across
 * the loops without a jump. Guard: a file that starts at 0 loops without a
 * gap before and after the fix. No display or graphics are needed.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>

#include <obs.h>
#include <util/darray.h>
#include <util/platform.h>
#include <util/threading.h>

#include <media-playback/media-playback.h>

#define RATE 48000
#define CLIP_MS 1000
#define START_MS 500
#define AUDIO_CHUNK 1024

#define W 64
#define H 64
#define FPS 30

/* play this long: three loops of the 1 s file and some */
#define PLAY_MS 3400
/* loops that must have been seen */
#define MIN_LOOPS 2
/* tolerated jump in the time stamps (they are exact within a loop) */
#define MAX_GAP_MS 10
/* tolerated distance between two video frames: one frame and the slack */
#define MAX_FRAME_STEP_MS (1000 / FPS + MAX_GAP_MS)

static bool files_ok;
static char audio_offset_path[512];
static char audio_zero_path[512];
static char av_offset_path[512];
/* only where FFmpeg has an MP3 encoder (LAME) */
static bool mp3_ok;
static char mp3_path[512];

/* ------------------------------------------------------------------------ */
/* Test files */

static bool write_packets(AVFormatContext *oc, AVCodecContext *enc, AVStream *st, AVPacket *pkt, int64_t start_ms)
{
	int ret;
	while ((ret = avcodec_receive_packet(enc, pkt)) >= 0) {
		pkt->stream_index = st->index;
		av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
		const int64_t offset = av_rescale_q(start_ms, (AVRational){1, 1000}, st->time_base);
		pkt->pts += offset;
		pkt->dts += offset;
		if (av_interleaved_write_frame(oc, pkt) < 0)
			return false;
	}
	return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF;
}

static bool flush_encoder(AVFormatContext *oc, AVCodecContext *enc, AVStream *st, AVPacket *pkt, int64_t start_ms)
{
	if (!enc)
		return true;
	if (avcodec_send_frame(enc, NULL) < 0)
		return false;
	return write_packets(oc, enc, st, pkt, start_ms);
}

/* Matroska with a PCM track (built into FFmpeg), a 440 Hz tone with no
 * silence in it, and with video an MPEG-4 part 2 track (also built in), all
 * starting at start_ms. */
static bool write_clip(const char *path, int64_t start_ms, bool with_video)
{
	AVFormatContext *oc = NULL;
	AVCodecContext *aenc = NULL, *venc = NULL;
	AVStream *as = NULL, *vs = NULL;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *af = av_frame_alloc();
	AVFrame *vf = av_frame_alloc();
	bool ok = false;

	if (!pkt || !af || !vf || avformat_alloc_output_context2(&oc, NULL, "matroska", path) < 0)
		goto done;

	const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
	if (!acodec || !(aenc = avcodec_alloc_context3(acodec)))
		goto done;
	aenc->sample_rate = RATE;
	aenc->sample_fmt = AV_SAMPLE_FMT_S16;
	aenc->time_base = (AVRational){1, RATE};
	av_channel_layout_default(&aenc->ch_layout, 1);
	if (avcodec_open2(aenc, acodec, NULL) < 0)
		goto done;
	if (!(as = avformat_new_stream(oc, NULL)) || avcodec_parameters_from_context(as->codecpar, aenc) < 0)
		goto done;
	as->time_base = aenc->time_base;

	if (with_video) {
		const AVCodec *vcodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
		if (!vcodec || !(venc = avcodec_alloc_context3(vcodec)))
			goto done;
		venc->width = W;
		venc->height = H;
		venc->pix_fmt = AV_PIX_FMT_YUV420P;
		venc->time_base = (AVRational){1, FPS};
		venc->framerate = (AVRational){FPS, 1};
		venc->gop_size = FPS;
		venc->max_b_frames = 0;
		if (oc->oformat->flags & AVFMT_GLOBALHEADER)
			venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if (avcodec_open2(venc, vcodec, NULL) < 0)
			goto done;
		if (!(vs = avformat_new_stream(oc, NULL)) || avcodec_parameters_from_context(vs->codecpar, venc) < 0)
			goto done;
		vs->time_base = venc->time_base;
	}

	if (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0)
		goto done;
	if (avformat_write_header(oc, NULL) < 0)
		goto done_io;

	vf->width = W;
	vf->height = H;
	vf->format = AV_PIX_FMT_YUV420P;
	if (with_video && av_frame_get_buffer(vf, 0) < 0)
		goto done_io;

	const int64_t total = (int64_t)RATE * CLIP_MS / 1000;
	const int frames = FPS * CLIP_MS / 1000;
	int64_t samples = 0;

	for (int n = 0; n < frames; n++) {
		if (with_video) {
			if (av_frame_make_writable(vf) < 0)
				goto done_io;
			for (int y = 0; y < H; y++)
				memset(vf->data[0] + y * vf->linesize[0], 16 + n * 7, W);
			for (int y = 0; y < H / 2; y++) {
				memset(vf->data[1] + y * vf->linesize[1], 128, W / 2);
				memset(vf->data[2] + y * vf->linesize[2], 128, W / 2);
			}
			vf->pts = n;
			if (avcodec_send_frame(venc, vf) < 0 || !write_packets(oc, venc, vs, pkt, start_ms))
				goto done_io;
		}

		const int64_t until = n == frames - 1 ? total : (int64_t)RATE * (n + 1) / FPS;
		while (samples < until) {
			const int chunk = (int)(until - samples < AUDIO_CHUNK ? until - samples : AUDIO_CHUNK);
			av_frame_unref(af);
			af->nb_samples = chunk;
			af->format = aenc->sample_fmt;
			af->sample_rate = RATE;
			if (av_channel_layout_copy(&af->ch_layout, &aenc->ch_layout) < 0 ||
			    av_frame_get_buffer(af, 0) < 0)
				goto done_io;
			int16_t *s = (int16_t *)af->data[0];
			for (int i = 0; i < chunk; i++)
				s[i] = (int16_t)(16000.0 * sin(2.0 * M_PI * 440.0 * (double)(samples + i) / RATE));
			af->pts = samples;
			samples += chunk;
			if (avcodec_send_frame(aenc, af) < 0 || !write_packets(oc, aenc, as, pkt, start_ms))
				goto done_io;
		}
	}

	if (!flush_encoder(oc, aenc, as, pkt, start_ms) || !flush_encoder(oc, venc, vs, pkt, start_ms))
		goto done_io;
	ok = av_write_trailer(oc) >= 0;

done_io:
	avio_closep(&oc->pb);
done:
	avcodec_free_context(&aenc);
	avcodec_free_context(&venc);
	av_frame_free(&af);
	av_frame_free(&vf);
	av_packet_free(&pkt);
	avformat_free_context(oc);
	return ok;
}

/* A 1 s .mp3 of the same tone. The encoder delay is in its LAME header, so
 * the demuxer starts it at that delay and the decoder trims the first frame
 * by it, as with any .mp3 a user loops. */
static bool write_mp3(const char *path)
{
	AVFormatContext *oc = NULL;
	AVCodecContext *enc = NULL;
	AVStream *st = NULL;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *f = av_frame_alloc();
	bool ok = false;

	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
	if (!codec || !pkt || !f || avformat_alloc_output_context2(&oc, NULL, "mp3", path) < 0)
		goto done;
	if (!(enc = avcodec_alloc_context3(codec)))
		goto done;
	enc->sample_rate = RATE;
	enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
	enc->bit_rate = 128000;
	enc->time_base = (AVRational){1, RATE};
	av_channel_layout_default(&enc->ch_layout, 1);
	if (avcodec_open2(enc, codec, NULL) < 0)
		goto done;
	if (!(st = avformat_new_stream(oc, NULL)) || avcodec_parameters_from_context(st->codecpar, enc) < 0)
		goto done;
	st->time_base = enc->time_base;

	if (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0)
		goto done;
	if (avformat_write_header(oc, NULL) < 0)
		goto done_io;

	const int64_t total = (int64_t)RATE * CLIP_MS / 1000;
	const int chunk = enc->frame_size > 0 ? enc->frame_size : AUDIO_CHUNK;
	for (int64_t samples = 0; samples < total; samples += chunk) {
		av_frame_unref(f);
		f->nb_samples = (int)(total - samples < chunk ? total - samples : chunk);
		f->format = enc->sample_fmt;
		f->sample_rate = RATE;
		if (av_channel_layout_copy(&f->ch_layout, &enc->ch_layout) < 0 || av_frame_get_buffer(f, 0) < 0)
			goto done_io;
		float *s = (float *)f->data[0];
		for (int i = 0; i < f->nb_samples; i++)
			s[i] = 0.5f * (float)sin(2.0 * M_PI * 440.0 * (double)(samples + i) / RATE);
		f->pts = samples;
		if (avcodec_send_frame(enc, f) < 0 || !write_packets(oc, enc, st, pkt, 0))
			goto done_io;
	}

	if (!flush_encoder(oc, enc, st, pkt, 0))
		goto done_io;
	ok = av_write_trailer(oc) >= 0;

done_io:
	avio_closep(&oc->pb);
done:
	avcodec_free_context(&enc);
	av_frame_free(&f);
	av_packet_free(&pkt);
	avformat_free_context(oc);
	return ok;
}

/* The start time the demuxer reports, so a muxer that moves the time stamps
 * shows up as a setup failure, not as a pass. */
static bool check_start(const char *path, int64_t start_ms)
{
	AVFormatContext *ic = NULL;
	bool ok = false;

	if (avformat_open_input(&ic, path, NULL, NULL) < 0)
		return false;
	if (avformat_find_stream_info(ic, NULL) >= 0) {
		int64_t start = ic->start_time == AV_NOPTS_VALUE ? 0 : ic->start_time;
		int64_t got_ms = start / 1000;
		ok = got_ms == start_ms;
		if (!ok)
			fprintf(stderr, "%s starts at %lld ms, expected %lld ms\n", path, (long long)got_ms,
				(long long)start_ms);
	}
	avformat_close_input(&ic);
	return ok;
}

static void temp_path(char *buf, size_t size, const char *name)
{
	const char *tmp = getenv("TMPDIR");
	if (!tmp || !*tmp)
		tmp = getenv("TEMP");
	if (!tmp || !*tmp)
		tmp = "/tmp";
	snprintf(buf, size, "%s/obs-loop-start-%llu-%s", tmp, (unsigned long long)os_gettime_ns(), name);
}

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);

	temp_path(audio_offset_path, sizeof(audio_offset_path), "audio-offset.mkv");
	temp_path(audio_zero_path, sizeof(audio_zero_path), "audio-zero.mkv");
	temp_path(av_offset_path, sizeof(av_offset_path), "av-offset.mkv");

	if (!write_clip(audio_offset_path, START_MS, false) || !write_clip(audio_zero_path, 0, false) ||
	    !write_clip(av_offset_path, START_MS, true)) {
		fprintf(stderr, "could not write the test clips\n");
		return -1;
	}
	if (!check_start(audio_offset_path, START_MS) || !check_start(audio_zero_path, 0) ||
	    !check_start(av_offset_path, START_MS)) {
		fprintf(stderr, "the test clips do not start where expected\n");
		return -1;
	}

	files_ok = true;

	temp_path(mp3_path, sizeof(mp3_path), "tone.mp3");
	mp3_ok = write_mp3(mp3_path);
	if (!mp3_ok)
		fprintf(stderr, "no MP3 encoder, the .mp3 case is skipped\n");
	return 0;
}

static int group_teardown(void **state)
{
	UNUSED_PARAMETER(state);
	os_unlink(audio_offset_path);
	os_unlink(audio_zero_path);
	os_unlink(av_offset_path);
	if (mp3_ok)
		os_unlink(mp3_path);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Player */

struct chunk {
	uint64_t ts;
	uint32_t frames;
	uint32_t rate;
};

struct player {
	media_playback_t *mp;
	pthread_mutex_t mutex;
	DARRAY(struct chunk) audio;
	DARRAY(uint64_t) video;
};

static void video_cb(void *opaque, struct obs_source_frame *f)
{
	struct player *p = opaque;
	pthread_mutex_lock(&p->mutex);
	da_push_back(p->video, &f->timestamp);
	pthread_mutex_unlock(&p->mutex);
}

static void preload_cb(void *opaque, struct obs_source_frame *f)
{
	UNUSED_PARAMETER(opaque);
	UNUSED_PARAMETER(f);
}

static void audio_cb(void *opaque, struct obs_source_audio *a)
{
	struct player *p = opaque;
	struct chunk c = {a->timestamp, a->frames, a->samples_per_sec};
	pthread_mutex_lock(&p->mutex);
	da_push_back(p->audio, &c);
	pthread_mutex_unlock(&p->mutex);
}

static void stop_cb(void *opaque)
{
	UNUSED_PARAMETER(opaque);
}

/* Plays the file looped for PLAY_MS and keeps the time stamps it sent. */
static void play_looped(struct player *p, const char *path)
{
	memset(p, 0, sizeof(*p));
	pthread_mutex_init(&p->mutex, NULL);

	struct mp_media_info info = {
		.opaque = p,
		.v_cb = video_cb,
		.v_preload_cb = preload_cb,
		.v_seek_cb = preload_cb,
		.a_cb = audio_cb,
		.stop_cb = stop_cb,
		.path = path,
		.speed = 100,
		.force_range = VIDEO_RANGE_DEFAULT,
		.is_local_file = true,
	};
	p->mp = media_playback_create(&info);
	assert_non_null(p->mp);
	media_playback_play(p->mp, true, false);
	os_sleep_ms(PLAY_MS);
	media_playback_destroy(p->mp);
	p->mp = NULL;
}

static void player_free(struct player *p)
{
	da_free(p->audio);
	da_free(p->video);
	pthread_mutex_destroy(&p->mutex);
}

/* Largest jump between the end of one audio chunk and the start of the
 * next; also returns how much audio was played. */
static int64_t max_audio_gap_ms(struct player *p, int64_t *played_ms)
{
	int64_t max_gap = 0;
	uint64_t samples = 0;

	for (size_t i = 0; i < p->audio.num; i++) {
		const struct chunk *c = &p->audio.array[i];
		samples += c->frames;
		if (!i)
			continue;
		const struct chunk *prev = &p->audio.array[i - 1];
		const int64_t end = (int64_t)prev->ts + (int64_t)prev->frames * 1000000000LL / prev->rate;
		const int64_t gap = llabs((int64_t)c->ts - end) / 1000000;
		if (gap > max_gap) {
			max_gap = gap;
			fprintf(stderr, "  audio: %lld ms jump at %.3f s of audio\n", (long long)gap,
				(double)samples / RATE);
		}
	}

	*played_ms = (int64_t)(samples * 1000 / RATE);
	return max_gap;
}

/* Largest step from one video frame to the next. */
static int64_t max_video_step_ms(struct player *p)
{
	int64_t max_step = 0;

	for (size_t i = 1; i < p->video.num; i++) {
		const int64_t step = llabs((int64_t)p->video.array[i] - (int64_t)p->video.array[i - 1]) / 1000000;
		if (step > max_step) {
			max_step = step;
			fprintf(stderr, "  video: %lld ms step after frame %zu\n", (long long)step, i - 1);
		}
	}
	return max_step;
}

/* Whether the audio ran on across at least MIN_LOOPS loops without a gap. */
static bool audio_loops_ok(const char *what, struct player *p)
{
	int64_t played_ms = 0;
	int64_t gap = max_audio_gap_ms(p, &played_ms);
	bool ok = true;

	fprintf(stderr, "%s: %lld ms of audio played, largest jump %lld ms\n", what, (long long)played_ms,
		(long long)gap);

	if (played_ms < (int64_t)CLIP_MS * MIN_LOOPS) {
		fprintf(stderr, "%s: only %lld ms of audio played, expected %d loops\n", what, (long long)played_ms,
			MIN_LOOPS);
		ok = false;
	}
	if (gap > MAX_GAP_MS) {
		fprintf(stderr, "%s: the audio jumps by %lld ms between two loops\n", what, (long long)gap);
		ok = false;
	}
	return ok;
}

/* Whether the video ran on across the loops without a frozen picture. */
static bool video_loops_ok(const char *what, struct player *p)
{
	int64_t step = max_video_step_ms(p);
	bool ok = true;

	fprintf(stderr, "%s: %zu video frames, largest step %lld ms\n", what, p->video.num, (long long)step);

	if (p->video.num < (size_t)(FPS * MIN_LOOPS)) {
		fprintf(stderr, "%s: only %zu video frames played\n", what, p->video.num);
		ok = false;
	}
	if (step > MAX_FRAME_STEP_MS) {
		fprintf(stderr, "%s: the video jumps by %lld ms between two loops\n", what, (long long)step);
		ok = false;
	}
	return ok;
}

static void audio_loop(const char *what, const char *path)
{
	assert_true(files_ok);

	struct player p;
	play_looped(&p, path);
	bool ok = audio_loops_ok(what, &p);
	player_free(&p);
	assert_true(ok);
}

/* The report: a file that does not start at 0 loops with a gap. */
static void loop_audio_late_start(void **state)
{
	UNUSED_PARAMETER(state);
	audio_loop("starts at 500 ms", audio_offset_path);
}

/* With video, the picture freezes for the same time between the loops. */
static void loop_video_late_start(void **state)
{
	UNUSED_PARAMETER(state);
	assert_true(files_ok);

	const char *what = "audio and video, starts at 500 ms";
	struct player p;
	play_looped(&p, av_offset_path);
	bool audio_ok = audio_loops_ok(what, &p);
	bool video_ok = video_loops_ok(what, &p);
	player_free(&p);

	assert_true(audio_ok);
	assert_true(video_ok);
}

/* The report: an .mp3. Its first frame is trimmed by the encoder delay, and
 * both that frame and the loops must go on without a jump. */
static void loop_mp3(void **state)
{
	UNUSED_PARAMETER(state);
	if (!mp3_ok)
		skip();
	audio_loop(".mp3", mp3_path);
}

/* Guard: a file that starts at 0 loops without a gap. */
static void loop_audio_zero_start(void **state)
{
	UNUSED_PARAMETER(state);
	audio_loop("starts at 0", audio_zero_path);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(loop_audio_late_start),
		cmocka_unit_test(loop_video_late_start),
		cmocka_unit_test(loop_mp3),
		cmocka_unit_test(loop_audio_zero_start),
	};

	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
