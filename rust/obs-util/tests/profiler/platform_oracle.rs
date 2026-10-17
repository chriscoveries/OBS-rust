//! The oracle's `os_breakpoint` / `os_oom` are loud aborts rather than libobs's
//! SIGTRAP, and these tests pin that. libobs raises SIGTRAP there
//! (`libobs/util/platform-nix.c:793` and `:798`), which a test harness cannot
//! absorb, so `oracle/platform_host.c` prints a marker and calls `abort()`: a
//! parity test that reaches the 0-size or failed-allocation path then dies
//! visibly instead of silently agreeing with a difference.
//!
//! Each case forks, because the thing under test is a guaranteed crash.

#![cfg(unix)]

use std::ffi::{CString, c_char, c_void};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use obs_c_oracle as _;
use obs_util::ffi::bmem as rs;

unsafe extern "C" {
    fn oracle_bmalloc(size: usize) -> *mut c_void;
    fn oracle_brealloc(ptr: *mut c_void, size: usize) -> *mut c_void;
    fn oracle_bcrash(format: *const c_char, ...) -> !;
}

/// How the child ended.
enum Outcome {
    /// It was killed by a signal: `WIFSIGNALED`.
    Killed,
    /// It called `_exit` or `exit` with this status.
    Exited(i32),
}

/// What the child does that is expected to terminate it.
#[derive(Debug)]
enum Op {
    BmallocZero,
    BreallocZero,
    BmallocOom,
    Bcrash,
}

fn child_body(op: &Op) {
    match op {
        // SAFETY: the allocation sizes and pointers are valid FFI inputs.
        Op::BmallocZero => {
            let _ = unsafe { oracle_bmalloc(0) };
        }
        Op::BreallocZero => {
            let _ = unsafe { oracle_brealloc(std::ptr::null_mut(), 0) };
        }
        Op::BmallocOom => {
            let _ = unsafe { oracle_bmalloc(usize::MAX / 2) };
        }
        Op::Bcrash => {
            let msg = CString::new("oracle-bcrash-probe").unwrap();
            // SAFETY: the format is a valid NUL-terminated C string with no conversions.
            unsafe { oracle_bcrash(msg.as_ptr()) };
        }
    }
}

fn unique_path(tag: &str) -> PathBuf {
    let n = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or(0);
    std::env::temp_dir().join(format!(
        "oracle-breakpoint-{}-{}-{}",
        tag,
        std::process::id(),
        n
    ))
}

/// Runs `op` in a forked child whose stderr is captured in a file, and reports
/// how it ended together with what it printed.
///
/// The child redirects fd 2 to the capture file before doing anything else, so
/// the marker the abort prints is readable by the parent after `waitpid`.
fn run_captured(op: &Op) -> (Outcome, String) {
    let path = unique_path(&format!("{op:?}"));
    let c_path = CString::new(path.to_string_lossy().to_string()).unwrap();

    // SAFETY: fork separates the child, which then only makes FFI calls with
    // valid arguments and terminates; the parent only waits and reads a file.
    let outcome = unsafe {
        let pid = libc::fork();
        assert!(pid >= 0, "fork failed");
        if pid == 0 {
            let fd = libc::open(
                c_path.as_ptr(),
                libc::O_WRONLY | libc::O_CREAT | libc::O_TRUNC,
                0o644,
            );
            assert!(fd >= 0, "capture file open failed");
            assert_eq!(libc::dup2(fd, 2), 2, "stderr redirect failed");
            libc::close(fd);

            /* if this returns, the oracle did not stop the process */
            child_body(op);
            libc::_exit(0);
        }

        let mut status: i32 = 0;
        let rc = libc::waitpid(pid, &mut status, 0);
        assert_eq!(rc, pid, "waitpid returned {rc}");
        if libc::WIFSIGNALED(status) {
            Outcome::Killed
        } else {
            Outcome::Exited(libc::WEXITSTATUS(status))
        }
    };

    let stderr = std::fs::read_to_string(&path).unwrap_or_default();
    let _ = std::fs::remove_file(&path);
    (outcome, stderr)
}

/// `oracle_bmalloc(0)`: libobs's path is a trap, so the oracle's is a kill.
///
/// Mirrors `test/cmocka/test_video_frame.c:230`, which forks for the same
/// reason: a guaranteed crash must be observed without taking the test binary
/// down.
#[test]
fn oracle_bmalloc_zero_dies_loudly() {
    let (outcome, stderr) = run_captured(&Op::BmallocZero);

    assert!(
        matches!(outcome, Outcome::Killed),
        "oracle_bmalloc(0) returned instead of dying: the oracle's os_breakpoint \
         is a silent stub again, so a parity test reaching this path would pass \
         against an oracle libobs traps in"
    );
    assert!(
        stderr.contains("oracle: os_breakpoint"),
        "the child died but did not print the marker; stderr was:\n{stderr}"
    );
}

/// `oracle_brealloc(NULL, 0)` reaches the same place through
/// `libobs/util/bmem.c:111`.
#[test]
fn oracle_brealloc_zero_dies_loudly() {
    let (outcome, stderr) = run_captured(&Op::BreallocZero);

    assert!(
        matches!(outcome, Outcome::Killed),
        "oracle_brealloc(NULL, 0) returned instead of dying"
    );
    assert!(
        stderr.contains("oracle: os_breakpoint"),
        "the child died but did not print the marker; stderr was:\n{stderr}"
    );
}

/// A failed allocation must reach the oracle's `os_oom` path and stop loudly.
#[test]
fn oracle_bmalloc_oom_dies_loudly() {
    let (outcome, stderr) = run_captured(&Op::BmallocOom);

    assert!(
        matches!(outcome, Outcome::Killed),
        "oversized oracle_bmalloc returned instead of dying"
    );
    assert!(
        stderr.contains("oracle: os_oom"),
        "the child died but did not print the OOM marker; stderr was:\n{stderr}"
    );
}

/// The oracle's `bcrash` is the real `base-variadic.c` one, which prints and
/// exits with status 0. That is precisely why the abort has to sit in front of
/// it: on its own, this path would end with a zero exit status, which a parent
/// process could easily read as success.
#[test]
fn oracle_bcrash_alone_exits_zero() {
    let (outcome, stderr) = run_captured(&Op::Bcrash);

    assert!(
        matches!(outcome, Outcome::Exited(0)),
        "bcrash ended with something other than exit 0"
    );
    assert!(
        stderr.contains("oracle-bcrash-probe"),
        "the crash handler did not print; stderr was:\n{stderr}"
    );
}

/// The port's own allocation paths are unaffected by the oracle's aborts, so
/// the tests above are not accidentally exercising a broken shim.
#[test]
fn aligned_allocations_still_work() {
    let p = unsafe { rs::bmalloc(64) };
    assert!(!p.is_null());
    assert_eq!(p as usize % 32, 0);
    unsafe { rs::bfree(p) };

    let q = unsafe { rs::brealloc(std::ptr::null_mut(), 32) };
    assert!(!q.is_null());
    assert_eq!(q as usize % 32, 0);
    unsafe { rs::bfree(q) };
}
