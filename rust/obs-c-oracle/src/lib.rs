//! Test-only: the original libobs C sources compiled with `oracle_`-prefixed
//! symbols, used as the reference in Tier 2 layout tests and Tier 3
//! differential tests (`docs/rust-port/testing-policy.md`). Delete each
//! oracle together with the C source it wraps.
//!
//! Exempt from the port crates' `deny(unsafe_code)` and from the platform
//! boundary Dylint (#297, #323): every item here is a raw `extern "C"`
//! binding to the original C, so `unsafe` and per-OS `cfg` are the crate's
//! purpose. It is a dev-dependency only and never links into libobs.
//!
//! Also provides test-only stand-ins for libobs base/platform symbols
//! (`os_breakpoint` and `os_oom` are weak on Unix in `oracle/test_stubs.c`,
//! so the Rust shims win; `os_fread_utf8` is in that file too;
//! `os_fopen` and friends from `oracle/file_serializer_host.c` (unix
//! `os_safe_replace` is `oracle_host_os_safe_replace` there); the platform.c
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
#[cfg(any(unix, windows))]
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
pub mod hotkey_name_map;
pub mod lexer;
pub mod nal;
pub mod obs_nvenc;
pub mod obs_properties;
pub mod path_extension;
pub mod pipe;
#[cfg(target_os = "macos")]
pub mod platform_cocoa;
#[cfg(unix)]
pub mod platform_nix;
pub mod platform_number;
#[cfg(all(windows, target_env = "msvc"))]
pub mod platform_windows;
pub mod profiler;
pub mod profiler_snapshot;
pub mod shader_param_names;
pub mod task;
pub mod text_lookup;
pub mod threading;
pub mod utf8;
pub mod util_uint64;
pub mod vec2;
pub mod video_fourcc;
pub mod video_frame;
pub mod video_io;
pub mod video_io_inlines;
pub mod video_matrices;
