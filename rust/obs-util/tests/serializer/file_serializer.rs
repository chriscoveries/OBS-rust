//! Tier 1: safe-core tests for `libobs/util/file-serializer.c`.
//!
//! Each test names the cmocka case in `test/cmocka/test_file_serializer.c` it
//! mirrors. Buffered-file-serializer cases stay in that C file. Callback
//! null checks and the null-path `init_safe` difference are in
//! `file_serializer_parity.rs`, because they are properties of the C ABI.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use obs_c_oracle as _;
use obs_util::file_serializer::{Input, Output, SeekType, seek_type_from_c, temp_file_name};

fn path_bytes(path: &Path) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        path.as_os_str().as_bytes().to_vec()
    }
    #[cfg(not(unix))]
    {
        path.to_str().unwrap().as_bytes().to_vec()
    }
}

fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        PathBuf::from(std::ffi::OsStr::from_bytes(bytes))
    }
    #[cfg(not(unix))]
    {
        PathBuf::from(std::str::from_utf8(bytes).unwrap())
    }
}

fn temp_path(path: &Path, ext: &str) -> PathBuf {
    path_from_bytes(&temp_file_name(&path_bytes(path), ext.as_bytes()))
}

struct Scratch(PathBuf);

impl Scratch {
    fn new() -> Self {
        static N: AtomicU64 = AtomicU64::new(0);
        let n = N.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "obs-file-serializer-{}-{}-{n}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        Self(dir)
    }

    fn join(&self, name: &str) -> PathBuf {
        self.0.join(name)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn write_whole(path: &Path, data: &[u8]) {
    let mut out = Output::create(&path_bytes(path)).unwrap();
    assert_eq!(out.write(data), data.len());
    drop(out);
}

/// `test_output_input_roundtrip`.
///
/// The bytes are what `s_w8(0xAA)`, `s_wl32(0x04030201)`, `s_wb16(0x1234)`
/// and `s_write("hello")` produce. Empty writes return 0. Those helpers, and
/// the "read on an output / write on an input" checks, live in `serializer.h`.
#[test]
fn test_output_input_roundtrip() {
    let scratch = Scratch::new();
    let path = scratch.join("a.bin");
    let expected = [
        0xAAu8, 0x01, 0x02, 0x03, 0x04, 0x12, 0x34, b'h', b'e', b'l', b'l', b'o',
    ];

    let mut out = Output::create(&path_bytes(&path)).unwrap();
    assert_eq!(out.write(&expected), expected.len());
    assert_eq!(out.write(&[]), 0);
    assert_eq!(out.pos(), expected.len() as i64);
    drop(out);

    assert_eq!(
        std::fs::metadata(&path).unwrap().len(),
        expected.len() as u64
    );

    let mut input = Input::open(&path_bytes(&path)).unwrap();
    let mut back = [0u8; 12];
    assert_eq!(input.read(&mut back), expected.len());
    assert_eq!(back, expected);
    assert_eq!(input.read(&mut back[..1]), 0);
    assert_eq!(input.pos(), expected.len() as i64);
}

/// `test_serialize_helper`, minus the NULL-serializer checks in `serializer.h`.
#[test]
fn test_serialize_helper() {
    let scratch = Scratch::new();
    let path = scratch.join("a.bin");
    let data = b"abcd";

    write_whole(&path, data);

    let mut input = Input::open(&path_bytes(&path)).unwrap();
    let mut back = [0u8; 4];
    assert_eq!(input.read(&mut back), 4);
    assert_eq!(&back, data);
}

/// `test_output_seek_overwrite`.
#[test]
fn test_output_seek_overwrite() {
    let scratch = Scratch::new();
    let path = scratch.join("a.bin");

    let mut out = Output::create(&path_bytes(&path)).unwrap();
    assert_eq!(out.pos(), 0);
    assert_eq!(out.write(b"0123456789"), 10);
    assert_eq!(out.pos(), 10);

    assert_eq!(out.seek(4, SeekType::Start), 4);
    assert_eq!(out.pos(), 4);
    assert_eq!(out.write(b"X"), 1);
    assert_eq!(out.pos(), 5);

    assert_eq!(out.seek(2, SeekType::Current), 7);
    assert_eq!(out.seek(-1, SeekType::End), 9);
    assert_eq!(out.write(b"Z"), 1);
    assert_eq!(out.pos(), 10);
    drop(out);

    assert_eq!(std::fs::metadata(&path).unwrap().len(), 10);
    assert_eq!(std::fs::read(&path).unwrap(), b"0123X5678Z");
}

/// `test_input_seek`.
#[test]
fn test_input_seek() {
    let scratch = Scratch::new();
    let path = scratch.join("a.bin");
    write_whole(&path, b"ABCDEFGHIJ");

    let mut input = Input::open(&path_bytes(&path)).unwrap();
    assert_eq!(input.pos(), 0);

    let mut buf = [0u8; 4];
    assert_eq!(input.read(&mut buf[..3]), 3);
    assert_eq!(&buf[..3], b"ABC");
    assert_eq!(input.pos(), 3);

    assert_eq!(input.seek(6, SeekType::Start), 6);
    assert_eq!(input.read(&mut buf[..2]), 2);
    assert_eq!(&buf[..2], b"GH");

    assert_eq!(input.seek(-4, SeekType::Current), 4);
    assert_eq!(input.read(&mut buf[..1]), 1);
    assert_eq!(&buf[..1], b"E");

    assert_eq!(input.seek(-2, SeekType::End), 8);
    assert_eq!(input.read(&mut buf[..2]), 2);
    assert_eq!(&buf[..2], b"IJ");

    assert_eq!(input.seek(0, SeekType::End), 10);
    assert_eq!(input.read(&mut buf[..1]), 0);

    assert_eq!(input.seek(8, SeekType::Start), 8);
    assert_eq!(input.read(&mut buf), 2);

    let pos = input.pos();
    assert_eq!(input.seek(-1, SeekType::Start), -1);
    assert_eq!(input.pos(), pos);
}

/// `test_input_missing_file`.
#[test]
fn test_input_missing_file() {
    let scratch = Scratch::new();
    let path = scratch.join("missing.bin");
    assert!(!path.exists());
    assert!(Input::open(&path_bytes(&path)).is_none());
}

/// `test_output_unwritable_path`. Buffered cases stay in the C test.
#[test]
fn test_output_unwritable_path() {
    let scratch = Scratch::new();
    let path = scratch.join("no_such_subdir/x.bin");
    assert!(Output::create(&path_bytes(&path)).is_none());
    assert!(Output::create_safe(&path_bytes(&path), b"tmp").is_none());
    assert!(!path.exists());
    assert!(!temp_path(&path, "tmp").exists());
}

/// `test_safe_rejects_bad_temp_ext`. A null extension is a C ABI case.
#[test]
fn test_safe_rejects_bad_temp_ext() {
    let scratch = Scratch::new();
    let path = scratch.join("safe.bin");
    assert!(Output::create_safe(&path_bytes(&path), b"").is_none());
    assert!(!path.exists());
    assert!(!temp_path(&path, "tmp").exists());
}

fn safe_new_target(ext: &str) {
    let scratch = Scratch::new();
    let path = scratch.join("safe.bin");
    let temp = temp_path(&path, ext);

    let mut out = Output::create_safe(&path_bytes(&path), ext.as_bytes()).unwrap();
    assert_eq!(out.write(b"payload"), 7);
    assert!(temp.exists());
    assert!(!path.exists());
    drop(out);

    assert!(!temp.exists());
    assert!(path.exists());
    assert_eq!(std::fs::metadata(&path).unwrap().len(), 7);
    assert_eq!(std::fs::read(&path).unwrap(), b"payload");
}

/// `test_safe_temp_ext_without_dot`.
#[test]
fn test_safe_temp_ext_without_dot() {
    safe_new_target("tmp");
}

/// `test_safe_temp_ext_with_dot`.
#[test]
fn test_safe_temp_ext_with_dot() {
    safe_new_target(".tmp");
}

/// `test_safe_replaces_existing`.
#[test]
fn test_safe_replaces_existing() {
    let scratch = Scratch::new();
    let path = scratch.join("safe.bin");
    let temp = temp_path(&path, "tmp");
    write_whole(&path, b"old");

    let mut out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert_eq!(out.write(b"newdata"), 7);
    assert!(temp.exists());
    assert_eq!(std::fs::metadata(&path).unwrap().len(), 3);
    drop(out);

    assert!(!temp.exists());
    assert_eq!(std::fs::metadata(&path).unwrap().len(), 7);
    assert_eq!(std::fs::read(&path).unwrap(), b"newdata");
}

/// `test_safe_seek_and_pos`.
#[test]
fn test_safe_seek_and_pos() {
    let scratch = Scratch::new();
    let path = scratch.join("safe.bin");

    let mut out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert_eq!(out.write(b"abcdef"), 6);
    assert_eq!(out.pos(), 6);
    assert_eq!(out.seek(1, SeekType::Start), 1);
    assert_eq!(out.write(b"Z"), 1);
    drop(out);

    assert_eq!(std::fs::read(&path).unwrap(), b"aZcdef");
}

#[test]
fn temp_file_name_matches_dstr() {
    assert_eq!(temp_file_name(b"safe.bin", b"tmp"), b"safe.bin.tmp");
    assert_eq!(temp_file_name(b"safe.bin", b".tmp"), b"safe.bin.tmp");
    assert_eq!(temp_file_name(b"", b"tmp"), b".tmp");
    assert_eq!(temp_file_name(b"safe.bin", b""), b"safe.bin.");
}

/// C keeps `SEEK_SET` for any code other than current or end. This is not
/// `array_serializer::seek_type_from_c`, which returns `None` outside 0..=2.
#[test]
fn seek_type_from_c_unknown_is_start() {
    assert_eq!(seek_type_from_c(0), SeekType::Start);
    assert_eq!(seek_type_from_c(1), SeekType::Current);
    assert_eq!(seek_type_from_c(2), SeekType::End);
    assert_eq!(seek_type_from_c(99), SeekType::Start);
    assert_eq!(seek_type_from_c(-1), SeekType::Start);
}

#[test]
fn safe_empty_commit_renames_empty_temp() {
    let scratch = Scratch::new();
    let path = scratch.join("empty.bin");
    let temp = temp_path(&path, "tmp");
    let out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert!(temp.exists());
    assert!(!path.exists());
    drop(out);
    assert!(!temp.exists());
    assert_eq!(std::fs::read(&path).unwrap(), b"");
}

/// At most 64 bytes, so a destination replaced by a `/dev/full` symlink
/// fails the comparison instead of reading forever.
#[cfg(unix)]
fn read_prefix(path: &Path) -> Vec<u8> {
    use std::io::Read;
    let mut buf = Vec::new();
    std::fs::File::open(path)
        .unwrap()
        .take(64)
        .read_to_end(&mut buf)
        .unwrap();
    buf
}

/// `safe_save_preserves_original_on_write_failure` in
/// `test/cmocka/test_file_serializer_safe.c`: a failed write must not replace
/// the destination.
#[cfg(target_os = "linux")]
#[test]
fn test_safe_preserves_original_on_write_failure() {
    let dir = Scratch::new();
    let path = dir.join("cache.bin");
    std::fs::write(&path, b"original").unwrap();
    std::os::unix::fs::symlink("/dev/full", temp_path(&path, "tmp")).unwrap();

    // Larger than the write buffer, so the error surfaces at once.
    let big = vec![0u8; 64 * 1024];
    let mut out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert!(out.write(&big) < big.len());
    drop(out);
    assert_eq!(read_prefix(&path), b"original");
}

/// `safe_save_preserves_original_on_flush_failure`: a small write is
/// buffered and succeeds; the full disk is only reported at the flush.
#[cfg(target_os = "linux")]
#[test]
fn test_safe_preserves_original_on_flush_failure() {
    let dir = Scratch::new();
    let path = dir.join("cache.bin");
    std::fs::write(&path, b"original").unwrap();
    std::os::unix::fs::symlink("/dev/full", temp_path(&path, "tmp")).unwrap();

    let mut out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert_eq!(out.write(b"replacement"), b"replacement".len());
    drop(out);
    assert_eq!(read_prefix(&path), b"original");
}

/// `safe_save_preserves_original_on_rename_failure` (Unix: Windows cannot
/// delete the still-open temporary file): the destination is
/// never unlinked before the temporary file is known to be in place.
#[cfg(unix)]
#[test]
fn test_safe_preserves_original_on_rename_failure() {
    let dir = Scratch::new();
    let path = dir.join("cache.bin");
    std::fs::write(&path, b"original").unwrap();

    let mut out = Output::create_safe(&path_bytes(&path), b"tmp").unwrap();
    assert_eq!(out.write(b"replacement"), b"replacement".len());
    std::fs::remove_file(temp_path(&path, "tmp")).unwrap();
    drop(out);
    assert_eq!(read_prefix(&path), b"original");
}
