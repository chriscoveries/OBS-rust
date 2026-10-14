//! Rust port of the CUDA/NVENC driver loader in `plugins/obs-nvenc`
//! (`load_cuda_lib` and `cuda_functions[]` in `cuda-load.c`,
//! `load_nvenc_lib` in `nvenc-load.c`; obs-rust/obs-studio#520).
//!
//! OBS never links or ships CUDA: it opens the user-mode driver libraries at
//! run time. This crate replaces the C's single hard-coded name with an
//! ordered candidate search ([`candidates`]) and returns typed, diagnosable
//! errors ([`LoadError`]) instead of a bare `NULL`. The safe core takes a
//! [`DynLoader`] so every path is testable without a GPU; the real loader
//! ([`SystemLoader`]) and the host's candidate lists are chosen in
//! `platform`, the crate's only host-specific module.
//!
//! [`ffi`] is the C shim behind `ENABLE_RUST_LIBOBS` that fills the plugin's
//! `CudaFunctions` from the loaded driver.

#![deny(unsafe_code)]

mod candidates;
mod error;
#[allow(unsafe_code)] // the C ABI shim
pub mod ffi;
mod loader;
mod platform;

pub use candidates::{
    Candidate, CandidateSource, Env, Host, Library, SystemEnv, candidates, candidates_for,
    default_candidates,
};
pub use error::{CudaError, LoadError, parse_glibc_requirement};
pub use loader::{
    CudaDriver, CudaVersion, DynLib, DynLoader, OPTIONAL_CUDA_SYMBOLS, Symbol, open_library,
    required_cuda_symbols,
};
pub use platform::SystemLoader;
