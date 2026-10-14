/* Oracle for the obs-nvenc driver loader (obs-rust/obs-studio#520).
 *
 * cuda-load.c and nvenc-load.c cannot be compiled whole here: they
 * need nv-codec-headers (ffnvcodec), glad and, on Windows, D3D11. build.rs
 * therefore copies the original lines of load_cuda_lib(), the
 * cuda_functions[] table and load_nvenc_lib() verbatim (with #line markers)
 * into OUT_DIR, and this wrapper compiles them under oracle_ names against:
 *
 *  - an os_dlopen stand-in that records every name it is asked for and
 *    fails, so each loader reports the exact names (and order) C tries;
 *  - a CudaFunctions stand-in and an offsetof that yields 0, because the
 *    parity test compares only the table's names, not CUDA struct offsets.
 *
 * The preprocessor conditions (_WIN32, _M_ARM64) are the original ones, so
 * each host checks its own branch. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Exported to the Rust parity test (-Wmissing-prototypes, #516). */
void oracle_nvenc_reset_opened(void);
size_t oracle_nvenc_num_opened_names(void);
const char *oracle_nvenc_opened_name(size_t idx);
bool oracle_load_cuda_lib(void);
bool oracle_load_nvenc_lib(void);
size_t oracle_num_cuda_funcs(void);
const char *oracle_cuda_func_name(size_t idx);

#define ORACLE_NVENC_MAX_OPENS 8
static const char *oracle_nvenc_opened[ORACLE_NVENC_MAX_OPENS];
static size_t oracle_nvenc_num_opened;

static void *oracle_nvenc_recording_dlopen(const char *path)
{
	if (oracle_nvenc_num_opened < ORACLE_NVENC_MAX_OPENS)
		oracle_nvenc_opened[oracle_nvenc_num_opened++] = path;
	return NULL;
}

void oracle_nvenc_reset_opened(void)
{
	oracle_nvenc_num_opened = 0;
}

size_t oracle_nvenc_num_opened_names(void)
{
	return oracle_nvenc_num_opened;
}

const char *oracle_nvenc_opened_name(size_t idx)
{
	return idx < oracle_nvenc_num_opened ? oracle_nvenc_opened[idx] : NULL;
}

#define os_dlopen oracle_nvenc_recording_dlopen
#define load_cuda_lib oracle_load_cuda_lib
#define load_nvenc_lib oracle_load_nvenc_lib

static void *cuda_lib;
static void *nvenc_lib;

#include "load_cuda_lib.inc"
#include "load_nvenc_lib.inc"

typedef struct CudaFunctions {
	char unused;
} CudaFunctions;
#undef offsetof
#define offsetof(type, member) 0

#include "cuda_functions.inc"

size_t oracle_num_cuda_funcs(void)
{
	return num_cuda_funcs;
}

const char *oracle_cuda_func_name(size_t idx)
{
	return idx < num_cuda_funcs ? cuda_functions[idx].name : NULL;
}
