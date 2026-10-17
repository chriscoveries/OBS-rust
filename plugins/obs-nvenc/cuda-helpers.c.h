#include "build/obs-nvenc.pch.h"

static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
CudaFunctions *cu = NULL;

bool cuda_get_error_desc(CUresult res, const char **name, const char **desc)
{
	if (cu->cuGetErrorName(res, name) != CUDA_SUCCESS || cu->cuGetErrorString(res, desc) != CUDA_SUCCESS)
		return false;

	return true;
}

bool cuda_error_check(struct nvenc_data *enc, CUresult res, const char *func, const char *call)
{
	if (res == CUDA_SUCCESS)
		return true;

	struct dstr message = {0};

	const char *name, *desc;
	if (cuda_get_error_desc(res, &name, &desc)) {
		dstr_printf(&message, "%s: CUDA call \"%s\" failed with %s (%d): %s", func, call, name, res, desc);
	} else {
		dstr_printf(&message, "%s: CUDA call \"%s\" failed with %d", func, call, res);
	}

	error("%s", message.array);
	obs_encoder_set_last_error(enc->encoder, message.array);

	dstr_free(&message);
	return false;
}

bool init_cuda(obs_encoder_t *encoder)
{
	bool success;

	pthread_mutex_lock(&init_mutex);
	success = init_cuda_internal(encoder);
	pthread_mutex_unlock(&init_mutex);

	return success;
}

void obs_cuda_load(void)
{
	pthread_mutex_init(&init_mutex, NULL);
}

void obs_cuda_unload(void)
{
	bfree(cu);
	pthread_mutex_destroy(&init_mutex);
}
