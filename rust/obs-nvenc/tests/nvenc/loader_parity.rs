//! Tier 3: the Rust candidate search and symbol table against the original
//! C in plugins/obs-nvenc (obs-rust/obs-studio#520), compiled by
//! `obs-c-oracle` (`oracle/obs_nvenc_loader.c`) with a recording `os_dlopen`.
//!
//! The Rust search must *start* with exactly the name C opens on this host
//! (`libcuda.so.1` / `nvcuda.dll`, `libnvidia-encode.so.1` /
//! `nvEncodeAPI64.dll` / `nvEncodeAPI.dll` on ARM64), so the default path
//! cannot regress; the extra fallbacks come after it. The required symbol
//! table must equal `cuda_functions[]` name for name, in order.
//!
//! Intentional differences from C: the fallbacks after the first name (C
//! tries one name only). The candidate input space is the fixed default list
//! with no override, so no property test applies; there is no random input.

use std::ffi::CStr;
use std::sync::Mutex;

use obs_c_oracle::obs_nvenc as oracle;
use obs_nvenc::{Host, Library, default_candidates, required_cuda_symbols};

/// The oracle's dlopen log is global C state.
static ORACLE: Mutex<()> = Mutex::new(());

fn c_opened_names(load: unsafe extern "C" fn() -> bool) -> Vec<String> {
    let _guard = ORACLE.lock().unwrap_or_else(|e| e.into_inner());
    // SAFETY: the oracle functions touch only their own static state, which
    // the mutex serialises; returned names are static string literals.
    unsafe {
        oracle::oracle_nvenc_reset_opened();
        assert!(!load(), "the recording os_dlopen always fails");
        (0..oracle::oracle_nvenc_num_opened_names())
            .map(|i| {
                CStr::from_ptr(oracle::oracle_nvenc_opened_name(i))
                    .to_string_lossy()
                    .into_owned()
            })
            .collect()
    }
}

fn rust_first_name(lib: Library) -> Option<String> {
    default_candidates(lib, Host::current())
        .first()
        .map(|c| c.name.to_string_lossy().into_owned())
}

/// macOS has no NVIDIA driver: C still compiles its non-Windows branch, but
/// Rust intentionally searches nothing there.
fn host_has_drivers() -> bool {
    Host::current() != Host::MacOs
}

#[test]
fn cuda_search_starts_with_c_name() {
    let c = c_opened_names(oracle::oracle_load_cuda_lib);
    assert_eq!(c.len(), 1, "load_cuda_lib opens exactly one name: {c:?}");
    if host_has_drivers() {
        assert_eq!(rust_first_name(Library::CudaDriver).as_ref(), Some(&c[0]));
    } else {
        assert_eq!(rust_first_name(Library::CudaDriver), None);
    }
}

#[test]
fn nvenc_search_starts_with_c_name() {
    let c = c_opened_names(oracle::oracle_load_nvenc_lib);
    assert_eq!(c.len(), 1, "load_nvenc_lib opens exactly one name: {c:?}");
    if host_has_drivers() {
        assert_eq!(rust_first_name(Library::NvEncode).as_ref(), Some(&c[0]));
    } else {
        assert_eq!(rust_first_name(Library::NvEncode), None);
    }
}

#[test]
fn required_symbols_equal_cuda_functions_table() {
    // SAFETY: read-only access to a static table of string literals.
    let c: Vec<String> = unsafe {
        (0..oracle::oracle_num_cuda_funcs())
            .map(|i| {
                CStr::from_ptr(oracle::oracle_cuda_func_name(i))
                    .to_string_lossy()
                    .into_owned()
            })
            .collect()
    };
    // The C table's #ifndef _WIN32 follows the build host, as does Host::current().
    assert_eq!(required_cuda_symbols(Host::current()), c);
}
