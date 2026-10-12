/* Exercise async texture lease dispatch through the production GPU encode thread. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <pthread.h>

#include <cmocka.h>

#include "obs-internal.h"

extern void *obs_gpu_encode_thread(void *data);

#define VIDEO_WIDTH 64
#define VIDEO_HEIGHT 64
#define ASYNC_ENCODER_ID "test_async_texture_encoder"
#define SYNC_ENCODER_ID "test_sync_texture_encoder"

struct observed_token {
	void *token;
	void (*release)(void *token);
};

struct encoder_observation {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	unsigned int encode_calls;
	unsigned int token_count;
	bool bad_texture_contract;
	struct observed_token tokens[2];
};

static struct encoder_observation async_observation = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.cond = PTHREAD_COND_INITIALIZER,
};
static struct encoder_observation sync_observation = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.cond = PTHREAD_COND_INITIALIZER,
};
static bool obs_up;

/* These internal call sites are no-ops for this synthetic frame harness. */
void send_off_encoder_packet(obs_encoder_t *encoder, bool success, bool received, struct encoder_packet *packet)
{
	UNUSED_PARAMETER(encoder);
	UNUSED_PARAMETER(success);
	UNUSED_PARAMETER(received);
	UNUSED_PARAMETER(packet);
}

void handle_encoder_group_reconfigure_request(obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(encoder);
}

bool video_pause_check(struct pause_data *pause, uint64_t timestamp)
{
	UNUSED_PARAMETER(pause);
	UNUSED_PARAMETER(timestamp);
	return false;
}

void video_output_inc_texture_frames(video_t *video)
{
	UNUSED_PARAMETER(video);
}

void video_output_inc_texture_skipped_frames(video_t *video)
{
	UNUSED_PARAMETER(video);
}

static const char *encoder_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "GPU lease test encoder";
}

static void *async_encoder_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(encoder);
	return &async_observation;
}

static void *sync_encoder_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(encoder);
	return &sync_observation;
}

static void encoder_destroy(void *data)
{
	UNUSED_PARAMETER(data);
}

static bool async_encode_texture(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
				 uint64_t *next_key, struct encoder_packet *packet, bool *received_packet)
{
	UNUSED_PARAMETER(pts);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	UNUSED_PARAMETER(packet);
	*received_packet = false;

	struct encoder_observation *observation = data;
	if (!texture || !texture->tex[0] || !texture->tex[1] || !texture->iosurface || !texture->lease_acquire ||
	    !texture->lease_release) {
		pthread_mutex_lock(&observation->mutex);
		observation->bad_texture_contract = true;
		pthread_mutex_unlock(&observation->mutex);
		return false;
	}

	void *token = texture->lease_acquire(texture->lease_context);
	if (!token)
		return false;

	pthread_mutex_lock(&observation->mutex);
	if (observation->token_count >= 2) {
		observation->bad_texture_contract = true;
		pthread_mutex_unlock(&observation->mutex);
		texture->lease_release(token);
		return false;
	}
	observation->tokens[observation->token_count++] = (struct observed_token){token, texture->lease_release};
	observation->encode_calls++;
	pthread_cond_broadcast(&observation->cond);
	pthread_mutex_unlock(&observation->mutex);
	return true;
}

static bool sync_encode_texture(void *data, struct encoder_texture *texture, int64_t pts, uint64_t lock_key,
				uint64_t *next_key, struct encoder_packet *packet, bool *received_packet)
{
	UNUSED_PARAMETER(pts);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	UNUSED_PARAMETER(packet);
	*received_packet = false;

	struct encoder_observation *observation = data;
	pthread_mutex_lock(&observation->mutex);
	observation->encode_calls++;
	if (!texture || !texture->tex[0] || !texture->tex[1] || texture->iosurface || texture->lease_context ||
	    texture->lease_acquire || texture->lease_release)
		observation->bad_texture_contract = true;
	pthread_cond_broadcast(&observation->cond);
	pthread_mutex_unlock(&observation->mutex);
	return true;
}

static void register_test_encoders(void)
{
	struct obs_encoder_info async_info = {
		.id = ASYNC_ENCODER_ID,
		.type = OBS_ENCODER_VIDEO,
		.codec = "h264",
		.get_name = encoder_name,
		.create = async_encoder_create,
		.destroy = encoder_destroy,
		.encode_texture2 = async_encode_texture,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_ASYNC_TEXTURE,
	};
	obs_register_encoder(&async_info);

	struct obs_encoder_info sync_info = async_info;
	sync_info.id = SYNC_ENCODER_ID;
	sync_info.create = sync_encoder_create;
	sync_info.encode_texture2 = sync_encode_texture;
	sync_info.caps = OBS_ENCODER_CAP_PASS_TEXTURE;
	obs_register_encoder(&sync_info);
}

static int group_setup(void **state)
{
	UNUSED_PARAMETER(state);
	obs_up = obs_startup("en-US", NULL, NULL);
	if (obs_up)
		register_test_encoders();
	return obs_up ? 0 : -1;
}

static int group_teardown(void **state)
{
	UNUSED_PARAMETER(state);
	if (obs_up) {
		obs_shutdown();
		obs_up = false;
	}
	return 0;
}

static bool wait_for_encode_calls(struct encoder_observation *observation, unsigned int expected)
{
	for (unsigned int i = 0; i < 500; i++) {
		pthread_mutex_lock(&observation->mutex);
		bool ready = observation->encode_calls >= expected;
		pthread_mutex_unlock(&observation->mutex);
		if (ready)
			return true;
		os_sleep_ms(10);
	}
	return false;
}

static void async_encode_thread_dispatches_and_recycles_duplicate_frame(void **state)
{
	UNUSED_PARAMETER(state);
	struct video_output_info info = {
		.name = "GPU lease dispatch test",
		.format = VIDEO_FORMAT_NV12,
		.fps_num = 30,
		.fps_den = 1,
		.width = VIDEO_WIDTH,
		.height = VIDEO_HEIGHT,
		.range = VIDEO_RANGE_PARTIAL,
		.colorspace = VIDEO_CS_709,
		.cache_size = 4,
	};
	video_t *output = NULL;
	assert_int_equal(video_output_open(&output, &info), VIDEO_OUTPUT_SUCCESS);

	obs_encoder_t *async_encoder = obs_video_encoder_create(ASYNC_ENCODER_ID, "async", NULL, NULL);
	obs_encoder_t *sync_encoder = obs_video_encoder_create(SYNC_ENCODER_ID, "sync", NULL, NULL);
	assert_non_null(async_encoder);
	assert_non_null(sync_encoder);
	((struct obs_encoder *)async_encoder)->context.data = &async_observation;
	((struct obs_encoder *)sync_encoder)->context.data = &sync_observation;

	struct obs_core_video_mix video = {.video = output, .gpu_async_texture_pool = true};
	assert_int_equal(pthread_mutex_init(&video.gpu_encoder_mutex, NULL), 0);
	assert_int_equal(os_sem_init(&video.gpu_encode_semaphore, 0), 0);
	assert_int_equal(os_event_init(&video.gpu_encode_inactive, OS_EVENT_TYPE_MANUAL), 0);
	da_push_back(video.gpu_encoders, &async_encoder);
	da_push_back(video.gpu_encoders, &sync_encoder);

	struct obs_tex_frame_lease_state lease_state = {.video = &video};
	obs_texture_lease_init(&lease_state.lease);
	assert_true(obs_texture_lease_begin_frame(&lease_state.lease));
	struct obs_tex_frame frame = {
		.tex = (gs_texture_t *)0x1,
		.tex_uv = (gs_texture_t *)0x2,
		.iosurface = (void *)0x3,
		.count = 2,
		.lease_state = &lease_state,
	};
	deque_push_back(&video.gpu_encoder_queue, &frame, sizeof(frame));

	pthread_t encode_thread;
	assert_int_equal(pthread_create(&encode_thread, NULL, obs_gpu_encode_thread, &video), 0);
	/* The worker discards one priming semaphore before consuming frames. */
	os_sem_post(video.gpu_encode_semaphore);
	os_sem_post(video.gpu_encode_semaphore);
	assert_true(wait_for_encode_calls(&async_observation, 1));
	os_event_wait(video.gpu_encode_inactive);

	/* Process the duplicate frame with its second independent lease token. */
	os_event_reset(video.gpu_encode_inactive);
	os_sem_post(video.gpu_encode_semaphore);
	assert_true(wait_for_encode_calls(&async_observation, 2));
	os_event_wait(video.gpu_encode_inactive);
	assert_true(wait_for_encode_calls(&sync_observation, 2));

	pthread_mutex_lock(&video.gpu_encoder_mutex);
	assert_int_equal(lease_state.lease.phase, OBS_TEXTURE_LEASE_RETIRED);
	assert_int_equal(lease_state.lease.outstanding, 2);
	pthread_mutex_unlock(&video.gpu_encoder_mutex);

	/* Completion may arrive out of submission order. The last release recycles once. */
	async_observation.tokens[1].release(async_observation.tokens[1].token);
	pthread_mutex_lock(&video.gpu_encoder_mutex);
	assert_int_equal(lease_state.lease.outstanding, 1);
	assert_int_equal(video.gpu_encoder_avail_queue.size, 0);
	pthread_mutex_unlock(&video.gpu_encoder_mutex);
	async_observation.tokens[0].release(async_observation.tokens[0].token);
	pthread_mutex_lock(&video.gpu_encoder_mutex);
	assert_true(obs_texture_lease_is_drained(&lease_state.lease));
	assert_int_equal(video.gpu_encoder_avail_queue.size, sizeof(struct obs_tex_frame));
	pthread_mutex_unlock(&video.gpu_encoder_mutex);

	pthread_mutex_lock(&async_observation.mutex);
	assert_false(async_observation.bad_texture_contract);
	assert_int_equal(async_observation.encode_calls, 2);
	pthread_mutex_unlock(&async_observation.mutex);
	pthread_mutex_lock(&sync_observation.mutex);
	assert_false(sync_observation.bad_texture_contract);
	assert_int_equal(sync_observation.encode_calls, 2);
	pthread_mutex_unlock(&sync_observation.mutex);

	os_atomic_set_bool(&video.gpu_encode_stop, true);
	os_sem_post(video.gpu_encode_semaphore);
	assert_int_equal(pthread_join(encode_thread, NULL), 0);

	deque_free(&video.gpu_encoder_queue);
	deque_free(&video.gpu_encoder_avail_queue);
	da_free(video.gpu_encoders);
	os_event_destroy(video.gpu_encode_inactive);
	os_sem_destroy(video.gpu_encode_semaphore);
	pthread_mutex_destroy(&video.gpu_encoder_mutex);
	video_output_close(output);
	obs_encoder_release(sync_encoder);
	obs_encoder_release(async_encoder);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(async_encode_thread_dispatches_and_recycles_duplicate_frame),
	};
	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
