use std::path::{Path, PathBuf};

/// FNV-1a 64-bit over the bytes that change what the oracle archive contains:
/// this script, every oracle/*.c and *.h, the libobs/deps files `cc` compiles,
/// and the flag list. zccache keys an rlib on argv + Rust inputs but never
/// hashes the native archive a build script bundles, so without this an
/// oracle-only change can be served a stale cached rlib (see
/// https://github.com/obs-rust/obs-studio — zccache cache-keying gap).
/// `env!()` in src/lib.rs turns the value into a rustc `env-dep`, which
/// zccache does fold into the artifact key. No new dependency: a simple
/// stable hash is enough for change detection.
fn oracle_input_hash(extra_files: &[PathBuf], flags: &[&str]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    let mut mix = |bytes: &[u8]| {
        for &b in bytes {
            h ^= u64::from(b);
            h = h.wrapping_mul(0x100000001b3);
        }
    };
    let hash_file = |path: &Path, mix: &mut dyn FnMut(&[u8])| {
        mix(path.to_string_lossy().as_bytes());
        mix(&std::fs::read(path).unwrap_or_else(|e| panic!("read {}: {e}", path.display())));
    };
    hash_file(
        Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("build.rs")
            .as_path(),
        &mut mix,
    );
    let mut hash_inputs: Vec<PathBuf> = Vec::new();
    collect_oracle_inputs(
        &Path::new(env!("CARGO_MANIFEST_DIR")).join("oracle"),
        &mut hash_inputs,
    );
    hash_inputs.extend(extra_files.iter().cloned());
    hash_inputs.sort();
    for path in &hash_inputs {
        hash_file(path, &mut mix);
    }
    for flag in flags {
        mix(flag.as_bytes());
    }
    h
}

fn collect_oracle_inputs(dir: &Path, out: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(dir).unwrap_or_else(|e| panic!("read {}: {e}", dir.display())) {
        let path = entry.expect("dir entry").path();
        if path.is_dir() {
            collect_oracle_inputs(&path, out);
        } else if path.extension().is_some_and(|e| e == "c" || e == "h") {
            out.push(path);
        }
    }
}

fn main() {
    let manifest = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    // No canonicalize(): on Windows it yields a verbatim `\\?\` path, and
    // MSVC cannot resolve `#include "util/..."` against such an include dir.
    // CARGO_MANIFEST_DIR is already absolute.
    let libobs = manifest
        .ancestors()
        .nth(2)
        .expect("rust/obs-c-oracle has a repo root two levels up")
        .join("libobs");

    let mut oracle = cc::Build::new();
    // Rust never fuses `a * b + c` into an FMA, but GCC/Clang may (clang's
    // default -ffp-contract=on does so on aarch64), which would make float
    // oracles such as vec2_norm (`x*x + y*y`) differ in the last bit from the
    // Rust port. Pin the C oracle to unfused IEEE operations. MSVC does not
    // contract under its default /fp:precise.
    let mut flags: Vec<&str> = Vec::new();
    if !oracle.get_compiler().is_like_msvc() {
        oracle.flag("-ffp-contract=off");
        flags.push("-ffp-contract=off");
        // libobs/util/sse-intrin.h takes SSE from SIMDe outside MSVC. The
        // stand-in keeps `cargo test` free of a SIMDe install.
        oracle.include("oracle/sse-shim");
    }
    // Every oracle/*.c is compiled (obs-rust/obs-studio#137): a port adds its
    // wrapper file, not a line here. `*_posix.c` wrappers are Unix-only
    // (pipe-posix.c needs <spawn.h>) and `*_windows.c` wrappers are
    // Windows-only (pipe-windows.c needs <windows.h>).
    let unix = std::env::var_os("CARGO_CFG_UNIX").is_some();
    let windows = std::env::var_os("CARGO_CFG_WINDOWS").is_some();
    let mut sources: Vec<PathBuf> = std::fs::read_dir(manifest.join("oracle"))
        .expect("read oracle/")
        .map(|e| e.expect("dir entry").path())
        .filter(|p| p.extension().is_some_and(|e| e == "c"))
        .filter(|p| unix || !p.to_string_lossy().ends_with("_posix.c"))
        .filter(|p| windows || !p.to_string_lossy().ends_with("_windows.c"))
        .collect();
    sources.sort();
    oracle.files(sources);
    // base.c's variadic entry points stay C in libobs; the oracle needs them too.
    oracle.file(libobs.join("util/base-variadic.c"));
    oracle
        .include(&libobs)
        // obs.h (for obs-hevc.c, obs-av1.c and obs-avc.c) needs the CMake-generated obsconfig.h.
        .include("oracle/obsconfig")
        // libobs/util/uthash.h includes <uthash.h>, a system header CMake
        // finds for the real build. The Rust test runners have no uthash
        // package, so the text-lookup oracle uses a vendored copy of the
        // same release (2.3.0, BSD-1-Clause, see vendor/uthash/LICENSE).
        .include(manifest.join("vendor/uthash"))
        .std("c11");
    // task.c calls pthread_mutex_*/pthread_create plus the os_event/os_sem
    // helpers from threading-*.c. On Unix both come from the real sources
    // and the system pthread. MSVC has no pthread: libobs builds against
    // deps/w32-pthreads, so the oracle compiles its single-file build and
    // the real threading-windows.c instead.
    let mut libobs_files = vec![libobs.join("util/base-variadic.c")];
    if oracle.get_compiler().is_like_msvc() {
        let deps = libobs.with_file_name("deps");
        oracle
            .include(deps.join("w32-pthreads"))
            .define("PTW32_STATIC_LIB", None)
            .file(deps.join("w32-pthreads/pthread.c"))
            .file(libobs.join("util/threading-windows.c"));
        flags.push("-DPTW32_STATIC_LIB");
        libobs_files.push(deps.join("w32-pthreads/pthread.c"));
        libobs_files.push(libobs.join("util/threading-windows.c"));
    } else {
        oracle.file(libobs.join("util/threading-posix.c"));
        libobs_files.push(libobs.join("util/threading-posix.c"));
    }
    println!(
        "cargo:rustc-env=OBS_ORACLE_INPUT_HASH={:016x}",
        oracle_input_hash(&libobs_files, &flags)
    );
    oracle.compile("obs_c_oracle");

    println!("cargo:rerun-if-changed=oracle");
    println!("cargo:rerun-if-changed=vendor");
    // Cargo scans a watched directory recursively, so any libobs source or
    // header an oracle includes reruns this script.
    println!("cargo:rerun-if-changed={}", libobs.display());
}
