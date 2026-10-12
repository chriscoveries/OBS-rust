//! Test-only: the original libobs C sources compiled with `oracle_`-prefixed
//! symbols, used as the reference in Tier 2 layout tests and Tier 3
//! differential tests (`docs/rust-port/testing-policy.md`). Delete each
//! oracle together with the C source it wraps.
//!
//! Also provides test-only stand-ins for libobs base/platform symbols
//! (`os_breakpoint`, `os_oom`, `os_fread_utf8` from `oracle/test_stubs.c`;
//! `os_fopen` and friends from `oracle/file_serializer_host.c`; the platform.c
//! string conversions from `oracle/platform_conv_host.c`; `blog`/`bcrash`
//! from `util/base-variadic.c`), so any test binary using the oracle resolves
//! them.

// Fingerprint of the native inputs build.rs feeds to `cc` (oracle/*.c, *.h,
// the libobs/deps sources it compiles, the flag list). Reading it with
// `env!` records a rustc `env-dep`, which the compilation cache folds into
// this crate's artifact key — without it a native-only change can be served
// a stale cached rlib whose bundled objects predate the change.
const _: &str = env!("OBS_ORACLE_INPUT_HASH");

pub mod array_serializer;
#[cfg(unix)]
pub mod audio_io;
pub mod audio_io_inlines;
pub mod audio_math;
pub mod av1;
pub mod avc;
pub mod base;
pub mod bitstream;
pub mod bmem;
pub mod buffered_file_serializer;
pub mod cf_tokenizer;
pub mod config_file;
pub mod crc32;
pub mod darray;
pub mod dstr;
pub mod encoder_packet;
pub mod file_serializer;
pub mod format_conversion;
pub mod frame_rate;
pub mod graphics_math;
pub mod hevc;
pub mod lexer;
pub mod nal;
pub mod path_extension;
pub mod pipe;
pub mod profiler;
pub mod profiler_snapshot;
pub mod task;
pub mod text_lookup;
pub mod threading;
pub mod utf8;
pub mod util_uint64;
pub mod vec2;
pub mod video_fourcc;
pub mod video_frame;
pub mod video_io_inlines;
pub mod video_matrices;
