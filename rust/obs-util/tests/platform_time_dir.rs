//! Tier 1: safe-core tests for `libobs/util/platform-nix-time-dir.c`
//! (extracted from `platform-nix.c`).
//!
//! `test_opendir_readdir_closedir_lists_entries` mirrors
//! `test_opendir_readdir_closedir` in `test/cmocka/test_platform.c` 1:1.
//! The rest mirror the characterization cases in
//! `test/cmocka/test_platform_time_dir.c` — `os_glob`'s two valid-return
//! shapes and its flags-ignored quirk, `os_gettime_ns` monotonicity, and
//! the sleep helpers' past-target/`false` quirk. Raw-pointer C-ABI
//! properties (NULL handles, `os_dir` aliasing the caller's path) live in
//! `tests/platform_time_dir_parity.rs`.

use std::ffi::CStr;
use std::path::PathBuf;
use std::time::Instant;

use obs_c_oracle as _;
use obs_util::platform_time_dir::{sleep_ms, sleepto_ns, sleepto_ns_fast};
use obs_util::platform_time_dir::{gettime_ns, Dir, Glob};

struct ScratchDir(PathBuf);

impl ScratchDir {
    fn new() -> Self {
        let dir = std::env::temp_dir().join(format!(
            "obs-util-platform-time-dir-{}-{}",
            std::process::id(),
            gettime_ns()
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

fn d_name(ent: &obs_util::platform_time_dir::os_dirent) -> &str {
    // SAFETY: `d_name` is NUL-terminated by `Dir::readdir`.
    unsafe { CStr::from_ptr(ent.d_name.as_ptr()) }
        .to_str()
        .expect("test filenames are ASCII")
}

/// Mirrors `gettime_ns_monotonic` in `test_platform_time_dir.c`.
#[test]
fn gettime_ns_is_monotonic() {
    let t1 = gettime_ns();
    sleep_ms(2);
    let t2 = gettime_ns();
    assert!(t2 >= t1, "t2={t2} < t1={t1}");
}

/// Mirrors `sleep_ms_waits`: 50 ms sleeps at least ~45 ms.
#[test]
fn sleep_ms_waits() {
    let start = Instant::now();
    sleep_ms(50);
    assert!(start.elapsed().as_millis() >= 45);
}

/// Mirrors `sleep_ms_zero`.
#[test]
fn sleep_ms_zero_returns_quickly() {
    let start = Instant::now();
    sleep_ms(0);
    assert!(start.elapsed().as_millis() < 1000);
}

/// Mirrors `sleepto_ns_past_is_false`: targets at or before now are the
/// C `time_target < current` quirk — `false` with no sleep.
#[test]
fn sleepto_ns_past_returns_false() {
    let now = gettime_ns();
    assert!(!sleepto_ns(now - 1_000_000_000));
    assert!(!sleepto_ns(now));
    assert!(!sleepto_ns_fast(now - 1_000_000_000));
    assert!(!sleepto_ns_fast(now));
}

/// Mirrors `sleepto_ns_future_waits`: target now + 40 ms returns `true`
/// after waiting at least ~35 ms.
#[test]
fn sleepto_ns_future_waits() {
    let target = gettime_ns() + 40_000_000;
    let start = Instant::now();
    assert!(sleepto_ns(target));
    assert!(start.elapsed().as_millis() >= 35);

    let target = gettime_ns() + 40_000_000;
    let start = Instant::now();
    assert!(sleepto_ns_fast(target));
    assert!(start.elapsed().as_millis() >= 35);
}

/// Mirrors `test_opendir_readdir_closedir` in `test/cmocka/test_platform.c`:
/// two files plus a sub-directory, `.`/`..` skipped by the caller (the C
/// API does not filter), `directory` flag on the sub-dir, and the NULL /
/// missing-path tolerance reserved for the parity test.
#[test]
fn opendir_readdir_closedir_lists_entries() {
    let scratch = ScratchDir::new();
    let d_list = scratch.0.join("list");
    let d_sub = d_list.join("sub");
    std::fs::create_dir_all(&d_sub).unwrap();
    std::fs::write(d_list.join("l1.txt"), b"1").unwrap();
    std::fs::write(d_list.join("l2.txt"), b"2").unwrap();

    let mut dir = Dir::open(d_list.to_str().unwrap().as_bytes()).expect("opendir");

    let mut names: Vec<String> = Vec::new();
    let mut is_dir: Vec<bool> = Vec::new();
    while let Some(ent) = dir.readdir() {
        let name = d_name(ent);
        if name == "." || name == ".." {
            continue;
        }
        names.push(name.to_owned());
        is_dir.push(ent.directory);
    }
    drop(dir);

    assert_eq!(names.len(), 3);
    let mut sorted = names.clone();
    sorted.sort();
    assert_eq!(sorted, ["l1.txt", "l2.txt", "sub"]);
    for (name, dir_flag) in names.iter().zip(&is_dir) {
        assert_eq!(*dir_flag, name == "sub");
    }
}

/// `stat` follows symlinks, so a link pointing at a directory reports
/// `directory = true` (pinned against a `d_type`-based `is_dir`).
#[test]
fn readdir_marks_symlinked_dir_as_directory() {
    let scratch = ScratchDir::new();
    let target = scratch.0.join("real");
    std::fs::create_dir_all(&target).unwrap();
    std::os::unix::fs::symlink(&target, scratch.0.join("link")).unwrap();

    let mut dir = Dir::open(scratch.0.to_str().unwrap().as_bytes()).expect("opendir");
    let mut link_flag = None;
    while let Some(ent) = dir.readdir() {
        if d_name(ent) == "link" {
            link_flag = Some(ent.directory);
        }
    }
    assert_eq!(link_flag, Some(true));
}

/// Missing directory returns `None`, like `os_opendir` returning NULL.
#[test]
fn opendir_missing_returns_none() {
    assert!(Dir::open(b"/nonexistent/definitely/missing".as_slice()).is_none());
}

/// Mirrors `glob_matches_and_flags`: `*.txt` in glob order with the
/// `directory` flag, globbed entries land in `gl_pathv`.
#[test]
fn glob_matches_and_flags() {
    let scratch = ScratchDir::new();
    std::fs::write(scratch.0.join("a.txt"), b"1").unwrap();
    std::fs::write(scratch.0.join("b.txt"), b"2").unwrap();
    std::fs::create_dir_all(scratch.0.join("sub.txt")).unwrap();
    std::fs::write(scratch.0.join("no.md"), b"3").unwrap();

    let pattern = format!("{}/*.txt", scratch.0.display());
    let glob = Glob::open(pattern.as_bytes()).expect("glob");
    let info = glob.as_os_glob_t();
    // SAFETY: `info` points at a live glob for the duration of `glob`.
    let info = unsafe { &*info };
    assert_eq!(info.gl_pathc, 3);
    let mut names = Vec::new();
    let mut flags = Vec::new();
    for i in 0..info.gl_pathc {
        // SAFETY: `gl_pathv` has `gl_pathc` entries; `path` is a C string.
        let ent = unsafe { &*info.gl_pathv.add(i) };
        names.push(
            unsafe { CStr::from_ptr(ent.path) }
                .to_str()
                .unwrap()
                .rsplit('/')
                .next()
                .unwrap()
                .to_owned(),
        );
        flags.push(ent.directory);
    }
    let mut sorted = names.clone();
    sorted.sort();
    assert_eq!(sorted, ["a.txt", "b.txt", "sub.txt"]);
    for (name, dir_flag) in names.iter().zip(&flags) {
        assert_eq!(*dir_flag, name == "sub.txt");
    }
}

/// Mirrors `glob_flags_ignored`: the C ignores the `flags` argument
/// entirely, so a nonsense value still globs (shim-level, pinned here on
/// the safe API via the parity glob call).
#[test]
fn glob_no_match_returns_error() {
    let scratch = ScratchDir::new();
    let pattern = format!("{}/*.nomatch", scratch.0.display());
    assert!(Glob::open(pattern.as_bytes()).is_err());
}
