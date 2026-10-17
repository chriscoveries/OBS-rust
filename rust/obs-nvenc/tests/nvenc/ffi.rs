//! Tier 1: the C ABI shim that fills the plugin's `CudaFunctions`
//! (obs-rust/obs-studio#524), driven by a fake driver so no GPU is needed.
//! The error texts are the C plugin's existing log strings.

use std::collections::HashSet;
use std::ffi::{CString, OsString, c_char, c_void};

use obs_nvenc::ffi::{
    OBS_NVENC_CUDA_FUNCS_FAILED, OBS_NVENC_CUDA_LIB_FAILED, OBS_NVENC_CUDA_OK, ObsNvencCudaEntry,
    fill_table,
};
use obs_nvenc::{
    Candidate, CandidateSource, CudaDriver, DynLib, DynLoader, Host, LoadError,
    OPTIONAL_CUDA_SYMBOLS, Symbol, required_cuda_symbols,
};

struct FakeLib(HashSet<String>);

impl DynLib for FakeLib {
    fn symbol(&self, name: &str) -> Option<Symbol> {
        self.0
            .contains(name)
            .then(|| Symbol::from_ptr(std::ptr::dangling_mut::<c_void>()))
            .flatten()
    }

    fn call_driver_get_version(&self, _sym: Symbol) -> (i32, i32) {
        (0, 12040)
    }
}

struct FakeLoader(Option<HashSet<String>>);

impl DynLoader for FakeLoader {
    fn open(&self, c: &Candidate) -> Result<Box<dyn DynLib>, String> {
        match &self.0 {
            Some(syms) => Ok(Box::new(FakeLib(syms.clone()))),
            None => Err(format!(
                "{}: cannot open shared object file: No such file or directory",
                c.name.to_string_lossy()
            )),
        }
    }
}

fn candidate() -> Candidate {
    Candidate {
        name: OsString::from("libcuda.so.1"),
        source: CandidateSource::SonameDefault,
    }
}

fn host() -> Host {
    Host::Linux
}

fn full_symbols() -> HashSet<String> {
    let mut s: HashSet<String> = required_cuda_symbols(host())
        .into_iter()
        .map(str::to_owned)
        .collect();
    s.extend(OPTIONAL_CUDA_SYMBOLS.iter().map(|s| (*s).to_owned()));
    s
}

fn load(syms: Option<HashSet<String>>) -> Result<CudaDriver, LoadError> {
    CudaDriver::load_from_host(&[candidate()], &FakeLoader(syms), host())
}

fn entries(names: &[CString]) -> Vec<ObsNvencCudaEntry> {
    names
        .iter()
        .enumerate()
        .map(|(i, n)| ObsNvencCudaEntry {
            offset: (i * size_of::<usize>()) as isize,
            name: n.as_ptr() as *const c_char,
        })
        .collect()
}

fn names(list: &[&str]) -> Vec<CString> {
    list.iter().map(|s| CString::new(*s).unwrap()).collect()
}

fn text(buf: &[u8]) -> String {
    let end = buf.iter().position(|&b| b == 0).expect("NUL-terminated");
    String::from_utf8(buf[..end].to_vec()).unwrap()
}

#[test]
fn fills_every_slot_from_the_driver() {
    let driver = load(Some(full_symbols())).unwrap();
    let list = names(&required_cuda_symbols(host()));
    let rows = entries(&list);
    let mut table = vec![0usize; rows.len()];
    let mut err = [0u8; 256];
    let rc = unsafe { fill_table(Ok(&driver), table.as_mut_ptr().cast(), &rows, &mut err) };
    assert_eq!(rc, OBS_NVENC_CUDA_OK);
    let want = std::ptr::dangling_mut::<c_void>() as usize;
    assert!(table.iter().all(|&p| p == want), "{table:?}");
}

#[test]
fn unknown_function_reports_functions_failed() {
    let driver = load(Some(full_symbols())).unwrap();
    let list = names(&["cuInit", "cuNotAFunction"]);
    let rows = entries(&list);
    let mut table = [0usize; 2];
    let mut err = [0u8; 256];
    let rc = unsafe { fill_table(Ok(&driver), table.as_mut_ptr().cast(), &rows, &mut err) };
    assert_eq!(rc, OBS_NVENC_CUDA_FUNCS_FAILED);
    assert_eq!(
        text(&err),
        "[obs-nvenc] Could not load function: cuNotAFunction"
    );
}

#[test]
fn missing_driver_symbol_reports_functions_failed() {
    let mut syms = full_symbols();
    syms.remove("cuInit");
    let loaded = load(Some(syms));
    let list = names(&["cuInit"]);
    let rows = entries(&list);
    let mut table = [0usize; 1];
    let mut err = [0u8; 256];
    let rc = unsafe { fill_table(loaded.as_ref(), table.as_mut_ptr().cast(), &rows, &mut err) };
    assert_eq!(rc, OBS_NVENC_CUDA_FUNCS_FAILED);
    assert!(
        text(&err).starts_with("[obs-nvenc] Could not load function: cuInit"),
        "{}",
        text(&err)
    );
}

#[test]
fn missing_library_keeps_the_test_process_log_string() {
    let loaded = load(None);
    let list = names(&["cuInit"]);
    let rows = entries(&list);
    let mut table = [0usize; 1];
    let mut err = [0u8; 512];
    let rc = unsafe { fill_table(loaded.as_ref(), table.as_mut_ptr().cast(), &rows, &mut err) };
    assert_eq!(rc, OBS_NVENC_CUDA_LIB_FAILED);
    assert!(
        text(&err).starts_with("[NVENC] Test process failed: cuda_lib"),
        "{}",
        text(&err)
    );
    assert_eq!(table, [0], "nothing is written when the library fails");
}

#[test]
fn long_messages_are_truncated_and_terminated() {
    let loaded = load(None);
    let mut err = [0xAAu8; 16];
    let rc = unsafe { fill_table(loaded.as_ref(), std::ptr::null_mut(), &[], &mut err) };
    assert_eq!(rc, OBS_NVENC_CUDA_LIB_FAILED);
    assert_eq!(text(&err), "[NVENC] Test pr");
}

#[test]
fn plugin_cmake_builds_the_c_loader_and_a_rust_twin() {
    // obs-nvenc (installed) keeps the C loader; obs-nvenc-with-rust, built
    // beside it and never installed, swaps in the shim (#1065).
    let cmake = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../plugins/obs-nvenc/CMakeLists.txt"),
    )
    .unwrap();
    assert!(
        cmake.contains(
            "target_sources(obs-nvenc PRIVATE ${_obs_nvenc_sources} build/obs-nvenc.cuda-load+.h.c)"
        ),
        "the installed plugin must build cuda-load.c.h"
    );
    assert!(
        cmake.contains(
            "add_library(obs-nvenc-with-rust MODULE ${_obs_nvenc_sources} build/obs-nvenc.cuda-load-rust+.h.c)"
        ),
        "the Rust twin must build cuda-load-rust.c.h"
    );
    assert!(
        cmake
            .contains("target_link_libraries(obs-nvenc-with-rust PRIVATE obs_nvenc_plugin-static)"),
        "only the Rust twin links the obs-nvenc-plugin staticlib"
    );
}

/// The `cuda_functions[]` rows between the braces, without the `#ifndef`
/// lines' surroundings, from one plugin source.
fn function_table(file: &str) -> String {
    let src = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../plugins/obs-nvenc")
            .join(file),
    )
    .unwrap();
    let start = src
        .find("static const cuda_function cuda_functions[] = {")
        .unwrap_or_else(|| panic!("{file}: no cuda_functions[]"));
    let end = start + src[start..].find("};").unwrap();
    src[start..end].to_owned()
}

#[test]
fn rust_build_uses_the_same_function_table_as_the_c_build() {
    assert_eq!(
        function_table("cuda-load.c.h"),
        function_table("cuda-load-rust.c.h")
    );
}
