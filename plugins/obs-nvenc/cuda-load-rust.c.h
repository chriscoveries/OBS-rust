#include "build/obs-nvenc.pch.h"

/* ENABLE_RUST_LIBOBS replacement for cuda-load.c: the library search and
 * symbol lookup live in rust/obs-nvenc (src/ffi). This file keeps what
 * belongs to the plugin: the CudaFunctions offsets, the logging and the
 * encoder error strings. cuda_functions[] must stay identical to the one in
 * cuda-load.c (tests/ffi.rs checks that). */

typedef struct cuda_function {
	ptrdiff_t offset;
	const char *name;
} cuda_function;

static const cuda_function cuda_functions[] = {
	{offsetof(CudaFunctions, cuInit), "cuInit"},

	{offsetof(CudaFunctions, cuDeviceGetCount), "cuDeviceGetCount"},
	{offsetof(CudaFunctions, cuDeviceGet), "cuDeviceGet"},
	{offsetof(CudaFunctions, cuDeviceGetAttribute), "cuDeviceGetAttribute"},

	{offsetof(CudaFunctions, cuCtxCreate), "cuCtxCreate_v2"},
	{offsetof(CudaFunctions, cuCtxDestroy), "cuCtxDestroy_v2"},
	{offsetof(CudaFunctions, cuCtxPushCurrent), "cuCtxPushCurrent_v2"},
	{offsetof(CudaFunctions, cuCtxPopCurrent), "cuCtxPopCurrent_v2"},

	{offsetof(CudaFunctions, cuArray3DCreate), "cuArray3DCreate_v2"},
	{offsetof(CudaFunctions, cuArrayDestroy), "cuArrayDestroy"},
	{offsetof(CudaFunctions, cuMemcpy2D), "cuMemcpy2D_v2"},

	{offsetof(CudaFunctions, cuGetErrorName), "cuGetErrorName"},
	{offsetof(CudaFunctions, cuGetErrorString), "cuGetErrorString"},

	{offsetof(CudaFunctions, cuMemHostRegister), "cuMemHostRegister_v2"},
	{offsetof(CudaFunctions, cuMemHostUnregister), "cuMemHostUnregister"},

#ifndef _WIN32
	{offsetof(CudaFunctions, cuGLGetDevices), "cuGLGetDevices_v2"},
	{offsetof(CudaFunctions, cuGraphicsGLRegisterImage), "cuGraphicsGLRegisterImage"},
	{offsetof(CudaFunctions, cuGraphicsUnregisterResource), "cuGraphicsUnregisterResource"},
	{offsetof(CudaFunctions, cuGraphicsMapResources), "cuGraphicsMapResources"},
	{offsetof(CudaFunctions, cuGraphicsUnmapResources), "cuGraphicsUnmapResources"},
	{offsetof(CudaFunctions, cuGraphicsSubResourceGetMappedArray), "cuGraphicsSubResourceGetMappedArray"},
#endif
};

static const size_t num_cuda_funcs = sizeof(cuda_functions) / sizeof(cuda_function);

/* rust/obs-nvenc/src/ffi/mod.rs. The row layout matches ObsNvencCudaEntry. */
#define OBS_NVENC_CUDA_OK 0
#define OBS_NVENC_CUDA_LIB_FAILED 1
#define OBS_NVENC_CUDA_FUNCS_FAILED 2
int obs_nvenc_cuda_fill(void *table, const cuda_function *entries, size_t count, char *err, size_t err_len);

bool init_cuda_internal(obs_encoder_t *encoder)
{
	static bool initialized = false;
	static bool success = false;

	if (initialized)
		return success;
	initialized = true;

	char err[1024] = {0};
	CudaFunctions *table = bzalloc(sizeof(CudaFunctions));
	int rc = obs_nvenc_cuda_fill(table, cuda_functions, num_cuda_funcs, err, sizeof(err));

	if (rc == OBS_NVENC_CUDA_LIB_FAILED) {
		/* "[NVENC] Test process failed: cuda_lib" and what was tried. */
		blog(LOG_WARNING, "%s", err);
		obs_encoder_set_last_error(encoder, "Loading CUDA library failed.");
		bfree(table);
		return false;
	}

	/* As before, cu is set once the library is open, even if a function is
	 * missing, so obs_cuda_unload() frees it. */
	cu = table;

	if (rc != OBS_NVENC_CUDA_OK) {
		blog(LOG_ERROR, "%s", err);
		obs_encoder_set_last_error(encoder, "Loading CUDA functions failed.");
		return false;
	}

	success = true;
	return true;
}

/* File-local macros (BUILD.md): out of the next file in the unity group. */
#undef OBS_NVENC_CUDA_OK
#undef OBS_NVENC_CUDA_LIB_FAILED
#undef OBS_NVENC_CUDA_FUNCS_FAILED
