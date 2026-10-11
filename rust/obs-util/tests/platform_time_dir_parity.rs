//! Tier 3: differential parity between the C oracle
//! (`libobs/util/platform-nix-time-dir.c`, symbols renamed `oracle_*`)
//! and the Rust `os_*` shims. Fixture trees are built with `std::fs` so
//! neither side touches the not-yet-ported `os_mkdir`/`os_write` helpers.
//!
//! Wall-clock helpers (`os_gettime_ns`, the sleeps) are not discriminable
//! by value — two sequential calls legitimately differ — so their pinned
//! cases cover only the observable contract (past-target `false`).

use std::ffi::{CStr, CString};
use std::path::PathBuf;

use obs_c_oracle::platform_nix_time_dir as c;
use obs_util::ffi::platform_time_dir as r;
use proptest::prelude::*;

struct ScratchDir(PathBuf);

impl ScratchDir {
    fn new(tag: &str) -> Self {
        let dir = std::env::temp_dir().join(format!(
            "obs-util-ptd-parity-{}-{}-{tag}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        Self(dir)
    }
}

impl Drop for ScratchDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn cstring(s: &str) -> CString {
    CString::new(s).unwrap()
}

/// `name`, `is_dir` pairs for every entry of `path` (`.`/`..` skipped,
/// like the C tests do), via the oracle.
unsafe fn oracle_listing(path: &CString) -> Vec<(String, bool)> {
    let dir = unsafe { c::oracle_os_opendir(path.as_ptr()) };
    let mut out = Vec::new();
    if !dir.is_null() {
        // SAFETY: `dir` is live until `oracle_os_closedir`.
        while let Some(ent) = unsafe { c::oracle_os_readdir(dir).as_ref() } {
            let name = unsafe { CStr::from_ptr(ent.d_name.as_ptr()) }
                .to_string_lossy()
                .into_owned();
            if name != "." && name != ".." {
                out.push((name, ent.directory));
            }
        }
        unsafe { c::oracle_os_closedir(dir) };
    }
    out
}

/// Same listing via the Rust shims; `None` marks `os_opendir` NULL.
unsafe fn rust_listing(path: &CString) -> Option<Vec<(String, bool)>> {
    let dir = unsafe { r::os_opendir(path.as_ptr()) };
    let mut out = Vec::new();
    if dir.is_null() {
        return None;
    }
    // SAFETY: `dir` is live until `os_closedir`.
    while let Some(ent) = unsafe { r::os_readdir(dir).as_ref() } {
        let name = unsafe { CStr::from_ptr(ent.d_name.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if name != "." && name != ".." {
            out.push((name, ent.directory));
        }
    }
    unsafe { r::os_closedir(dir) };
    Some(out)
}

/// `ret`, then sorted (`name`, `is_dir`) entries, via the oracle glob.
unsafe fn oracle_glob(pattern: &CString, flags: i32) -> (i32, Vec<(String, bool)>) {
    let mut pglob = core::ptr::null_mut();
    let ret = unsafe { c::oracle_os_glob(pattern.as_ptr(), flags, &raw mut pglob) };
    let mut out = Vec::new();
    if !pglob.is_null() {
        let info = unsafe { &*pglob };
        for i in 0..info.gl_pathc {
            let ent = unsafe { &*info.gl_pathv.add(i) };
            out.push((
                unsafe { CStr::from_ptr(ent.path) }
                    .to_string_lossy()
                    .into_owned(),
                ent.directory,
            ));
        }
        unsafe { c::oracle_os_globfree(pglob) };
    }
    (ret, out)
}

/// Same via the Rust shims.
unsafe fn rust_glob(pattern: &CString, flags: i32) -> (i32, Vec<(String, bool)>) {
    let mut pglob = core::ptr::null_mut();
    let ret = unsafe { r::os_glob(pattern.as_ptr(), flags, &raw mut pglob) };
    let mut out = Vec::new();
    if !pglob.is_null() {
        let info = unsafe { &*pglob };
        for i in 0..info.gl_pathc {
            let ent = unsafe { &*info.gl_pathv.add(i) };
            out.push((
                unsafe { CStr::from_ptr(ent.path) }
                    .to_string_lossy()
                    .into_owned(),
                ent.directory,
            ));
        }
        unsafe { r::os_globfree(pglob) };
    }
    (ret, out)
}

/// Fixture names: legal on ext4, exercises the non-`.`/`..` paths; the
/// generator can also yield sub-directories.
fn name_strategy() -> impl Strategy<Value = Vec<(String, bool)>> {
    proptest::collection::vec(
        ("[a-z][a-z0-9._-]{0,15}", proptest::bool::weighted(0.25)),
        0..6,
    )
    .prop_map(|v| {
        let mut seen = std::collections::BTreeSet::new();
        v.into_iter()
            .map(|(mut n, is_dir)| {
                n.push_str(if is_dir { ".d" } else { ".txt" });
                (n, is_dir)
            })
            .filter(|(n, _)| seen.insert(n.clone()))
            .collect()
    })
}

proptest! {
    /// `os_opendir`/`os_readdir`/`os_closedir` over a generated fixture:
    /// oracle and shim produce the same (name, is_dir) set.
    #[test]
    fn readdir_parity(entries in name_strategy()) {
        let scratch = ScratchDir::new("readdir");
        for (name, is_dir) in &entries {
            let p = scratch.0.join(name);
            if *is_dir {
                std::fs::create_dir(&p).unwrap();
            } else {
                std::fs::write(&p, b"x").unwrap();
            }
        }
        let path = cstring(scratch.0.to_str().unwrap());
        let mut o = unsafe { oracle_listing(&path) };
        let mut s = unsafe { rust_listing(&path) }.expect("shim opendir");
        o.sort();
        s.sort();
        prop_assert_eq!(o, s);
    }

    /// `os_glob` over the same fixture: identical ret codes and entries
    /// for `*.txt`, `*.d` and a never-matching extension — including the
    /// flags-ignored quirk (each pattern also run with flags=7 on both).
    #[test]
    fn glob_parity(entries in name_strategy()) {
        let scratch = ScratchDir::new("glob");
        for (name, is_dir) in &entries {
            let p = scratch.0.join(name);
            if *is_dir {
                std::fs::create_dir(&p).unwrap();
            } else {
                std::fs::write(&p, b"x").unwrap();
            }
        }
        for pat in ["*.txt", "*.d", "*.zzz"] {
            let pattern = cstring(&format!("{}/{pat}", scratch.0.display()));
            for flags in [0, 7] {
                let (oret, mut o) = unsafe { oracle_glob(&pattern, flags) };
                let (sret, mut s) = unsafe { rust_glob(&pattern, flags) };
                prop_assert_eq!(oret, sret);
                o.sort();
                s.sort();
                prop_assert_eq!(o, s);
            }
        }
    }
}

/// `os_opendir` on a missing path returns NULL on both; `os_readdir(NULL)`
/// and `os_closedir(NULL)`/`os_globfree(NULL)` are tolerated on both.
#[test]
fn null_and_missing_tolerance() {
    let missing = cstring("/nonexistent/definitely/missing");
    unsafe {
        assert!(c::oracle_os_opendir(missing.as_ptr()).is_null());
        assert!(r::os_opendir(missing.as_ptr()).is_null());
        assert!(c::oracle_os_readdir(core::ptr::null_mut()).is_null());
        assert!(r::os_readdir(core::ptr::null_mut()).is_null());
        c::oracle_os_closedir(core::ptr::null_mut());
        r::os_closedir(core::ptr::null_mut());
        c::oracle_os_globfree(core::ptr::null_mut());
        r::os_globfree(core::ptr::null_mut());
    }
}

/// `os_globfree` frees an oracle-created glob through the Rust shim too —
/// the allocations are one bmem pool either way. Pinned because the
/// shim's `from_os_glob_t` cast is the only place that can go wrong.
#[test]
fn globfree_crosses_the_boundary() {
    let scratch = ScratchDir::new("crossfree");
    std::fs::write(scratch.0.join("a.txt"), b"1").unwrap();
    let pattern = cstring(&format!("{}/*.txt", scratch.0.display()));
    unsafe {
        let mut pglob = core::ptr::null_mut();
        assert_eq!(c::oracle_os_glob(pattern.as_ptr(), 0, &raw mut pglob), 0);
        assert!(!pglob.is_null());
        // Free the oracle's glob through the Rust shim: same bmem pool.
        r::os_globfree(pglob as *mut _);

        let mut pglob = core::ptr::null_mut();
        assert_eq!(r::os_glob(pattern.as_ptr(), 0, &raw mut pglob), 0);
        assert!(!pglob.is_null());
        // And the reverse.
        c::oracle_os_globfree(pglob as *mut _);
    }
}

/// Past targets return `false` on both without sleeping; a near-future
/// target returns `true` on both. Each side gets its own fresh deadline:
/// the oracle's sleep would consume a shared one. An exactly-`now`
/// target is deliberately untested — `time_target < current` is false
/// only while the two reads share a nanosecond, so the branch is
/// nondeterministic by design, not by port.
#[test]
fn sleepto_parity() {
    let past = unsafe { c::oracle_os_gettime_ns() } - 1_000_000_000;
    unsafe {
        assert!(!c::oracle_os_sleepto_ns(past));
        assert!(!r::os_sleepto_ns(past));
        assert!(!c::oracle_os_sleepto_ns_fast(past));
        assert!(!r::os_sleepto_ns_fast(past));

        assert!(c::oracle_os_sleepto_ns(
            c::oracle_os_gettime_ns() + 5_000_000
        ));
        assert!(r::os_sleepto_ns(r::os_gettime_ns() + 5_000_000));
        assert!(c::oracle_os_sleepto_ns_fast(
            c::oracle_os_gettime_ns() + 5_000_000
        ));
        assert!(r::os_sleepto_ns_fast(r::os_gettime_ns() + 5_000_000));
    }
}
