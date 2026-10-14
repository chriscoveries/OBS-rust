//! Regression tests for obs-rust/obs-studio#489: a failed final `close(2)` of
//! the written config file must make `config_save` return `CONFIG_ERROR`, and
//! `config_save_safe` must then leave the original file alone (the C
//! reference checks `fclose`). The failure is injected by interposing
//! `close`, so these tests are Linux-only and live in their own test binary.
#![cfg(target_os = "linux")]
#![allow(unsafe_code)]

use std::ffi::c_int;
use std::path::PathBuf;
use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use obs_c_oracle as _;
use obs_util::config_file::{CONFIG_ERROR, CONFIG_SUCCESS, Config, OpenType};

/// Only descriptors whose path contains this marker are affected.
const MARKER: &str = "close-fail-489";

static ARMED: AtomicBool = AtomicBool::new(false);
static MATCHED_CLOSES: AtomicUsize = AtomicUsize::new(0);
static SERIAL: Mutex<()> = Mutex::new(());

/// Interposed `close`: really closes, then reports `EIO` once when armed and
/// the descriptor is a marked file.
#[unsafe(no_mangle)]
pub extern "C" fn close(fd: c_int) -> c_int {
    let marked = std::fs::read_link(format!("/proc/self/fd/{fd}"))
        .map(|p| p.to_string_lossy().contains(MARKER))
        .unwrap_or(false);
    let rc = unsafe { libc::syscall(libc::SYS_close, fd) } as c_int;
    if marked {
        MATCHED_CLOSES.fetch_add(1, Ordering::SeqCst);
        if ARMED.swap(false, Ordering::SeqCst) {
            unsafe { *libc::__errno_location() = libc::EIO };
            return -1;
        }
    }
    rc
}

struct Scratch(PathBuf);

impl Scratch {
    fn new(tag: &str) -> Self {
        let dir = std::env::temp_dir().join(format!("{MARKER}-{}-{tag}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        Self(dir)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn path_bytes(p: &std::path::Path) -> Vec<u8> {
    p.to_string_lossy().into_owned().into_bytes()
}

#[test]
fn config_save_reports_close_failure() {
    let _g = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    let scratch = Scratch::new("save");
    let path = scratch.0.join("settings.ini");
    let c = Config::create(&path_bytes(&path)).unwrap();
    c.set_string(b"General", b"Name", b"new settings");

    MATCHED_CLOSES.store(0, Ordering::SeqCst);
    ARMED.store(true, Ordering::SeqCst);
    let ret = c.save();
    ARMED.store(false, Ordering::SeqCst);

    assert_eq!(
        MATCHED_CLOSES.load(Ordering::SeqCst),
        1,
        "one close attempt"
    );
    assert_eq!(ret, CONFIG_ERROR);
}

#[test]
fn config_save_safe_keeps_original_on_close_failure() {
    let _g = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    let scratch = Scratch::new("safe");
    let path = scratch.0.join("settings.ini");
    let backup = scratch.0.join("settings.ini.bak");

    let c = Config::create(&path_bytes(&path)).unwrap();
    c.set_string(b"General", b"Name", b"original settings");
    assert_eq!(c.save(), CONFIG_SUCCESS);
    c.set_string(b"General", b"Name", b"new settings");

    MATCHED_CLOSES.store(0, Ordering::SeqCst);
    ARMED.store(true, Ordering::SeqCst);
    let ret = c.save_safe(Some(b"tmp"), Some(b"bak"));
    ARMED.store(false, Ordering::SeqCst);
    drop(c);

    assert_eq!(
        MATCHED_CLOSES.load(Ordering::SeqCst),
        1,
        "one close attempt"
    );
    assert_eq!(ret, CONFIG_ERROR);
    let saved = Config::open(&path_bytes(&path), OpenType::Existing).unwrap();
    assert_eq!(
        saved.get_string(b"General", b"Name").as_deref(),
        Some(b"original settings".as_slice())
    );
    assert!(!backup.exists(), "original must not be moved to the backup");
}
