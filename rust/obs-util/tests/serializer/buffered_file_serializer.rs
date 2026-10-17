//! Tier 1: safe-core tests for `libobs/util/buffered-file-serializer.c`.
//!
//! Each test names the cmocka case in `test/cmocka/test_file_serializer.c` it
//! mirrors. Callback checks live in `buffered_file_serializer_parity.rs`.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use obs_c_oracle as _;
use obs_util::buffered_file_serializer::Output;

struct Scratch(PathBuf);

impl Scratch {
    fn new() -> Self {
        static N: AtomicU64 = AtomicU64::new(0);
        let n = N.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "obs-buffered-file-serializer-{}-{}-{n}",
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

fn read_file(path: &Path) -> Vec<u8> {
    std::fs::read(path).unwrap()
}

/// `test_buffered_defaults`.
#[test]
fn test_buffered_defaults() {
    let scratch = Scratch::new();
    let path = scratch.join("buf.bin");
    let data: Vec<u8> = (0..20).map(|i| (i * 3 + 1) as u8).collect();

    let out = Output::create_defaults(&path).unwrap();
    assert_eq!(out.pos(), 0);
    assert_eq!(out.write(&data), data.len());
    assert_eq!(out.pos(), data.len() as i64);
    assert_eq!(out.write(&[]), 0);
    assert_eq!(out.pos(), data.len() as i64);
    drop(out);

    assert_eq!(read_file(&path), data);
}

/// `test_buffered_small_chunks`.
#[test]
fn test_buffered_small_chunks() {
    let scratch = Scratch::new();
    let path = scratch.join("buf.bin");
    let data: Vec<u8> = (0..1000).map(|i| (i ^ (i >> 8) ^ 0x5A) as u8).collect();

    let out = Output::create(&path, 64, 8).unwrap();
    assert_eq!(out.write(&data), data.len());
    assert_eq!(out.pos(), data.len() as i64);
    drop(out);

    assert_eq!(read_file(&path), data);
}

/// `test_buffered_seek_overwrite`.
///
/// `SERIALIZE_SEEK_END` subtracts from the virtual write position. It does
/// not use the file size. 21 - 5 = 16. Characterized in the cmocka test.
#[test]
fn test_buffered_seek_overwrite() {
    let scratch = Scratch::new();
    let path = scratch.join("buf.bin");
    let mut expected: Vec<u8> = (0..100).map(|i| i as u8).collect();

    let out = Output::create(&path, 64, 8).unwrap();
    assert_eq!(out.write(&expected), expected.len());
    assert_eq!(out.pos(), 100);

    assert_eq!(
        out.seek(
            10,
            Some(obs_util::buffered_file_serializer::SeekType::Start)
        ),
        10
    );
    assert_eq!(out.pos(), 10);
    assert_eq!(out.write(b"XYZ"), 3);
    assert_eq!(out.pos(), 13);
    expected[10..13].copy_from_slice(b"XYZ");

    assert_eq!(
        out.seek(
            7,
            Some(obs_util::buffered_file_serializer::SeekType::Current)
        ),
        20
    );
    assert_eq!(out.write(b"Q"), 1);
    expected[20] = b'Q';
    assert_eq!(out.pos(), 21);

    assert_eq!(
        out.seek(5, Some(obs_util::buffered_file_serializer::SeekType::End)),
        16
    );
    assert_eq!(out.pos(), 16);
    assert_eq!(out.write(b"E"), 1);
    expected[16] = b'E';
    assert_eq!(out.pos(), 17);
    drop(out);

    assert_eq!(std::fs::metadata(&path).unwrap().len(), 100);
    assert_eq!(read_file(&path), expected);
}

/// `test_buffered_seek_past_end`. The gap bytes are left for the filesystem.
/// This checks the ends, as the cmocka test does.
#[test]
fn test_buffered_seek_past_end() {
    let scratch = Scratch::new();
    let path = scratch.join("buf.bin");

    let out = Output::create(&path, 64, 8).unwrap();
    assert_eq!(out.write(b"ab"), 2);
    assert_eq!(
        out.seek(6, Some(obs_util::buffered_file_serializer::SeekType::Start)),
        6
    );
    assert_eq!(out.write(b"cd"), 2);
    assert_eq!(out.pos(), 8);
    drop(out);

    let back = read_file(&path);
    assert_eq!(back.len(), 8);
    assert_eq!(&back[..2], b"ab");
    assert_eq!(&back[6..], b"cd");
}

/// `test_output_unwritable_path`, buffered inits only.
#[test]
fn test_output_unwritable_path() {
    let scratch = Scratch::new();
    let path = scratch.join("no_such_subdir").join("x.bin");

    assert!(Output::create_defaults(&path).is_none());
    assert!(Output::create(&path, 64, 8).is_none());
    assert!(!path.exists());
}

/// One chunk under 64 KiB stays out of the file until free. A second chunk
/// would force the first one out, so this write is exactly one chunk.
#[test]
fn small_write_stays_off_disk_until_free() {
    let scratch = Scratch::new();
    let path = scratch.join("buf.bin");
    let payload = b"01234567";

    let out = Output::create(&path, 64, 8).unwrap();
    assert_eq!(out.write(payload), payload.len());
    assert_eq!(std::fs::metadata(&path).unwrap().len(), 0);
    drop(out);

    assert_eq!(read_file(&path), payload);
}
