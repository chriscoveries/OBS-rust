//! Tier 1: safe-core tests for `libobs/util/platform-nix.c`.
//!
//! Each test names the cmocka case in `test/cmocka/test_platform.c` it
//! mirrors. Cases that call `platform.c` (`os_mkdirs`, `os_fopen`, the
//! quick-read helpers) are not this unit.

#![cfg(unix)]

use obs_c_oracle as _;

use std::fs;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};

use obs_util::platform::unix::nix::{self, DirEnt};

fn bytes(path: &Path) -> &[u8] {
    path.as_os_str().as_bytes()
}

fn scratch() -> std::path::PathBuf {
    static N: AtomicU64 = AtomicU64::new(0);
    let id = N.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!("obs-platform-nix-{}-{}", std::process::id(), id));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

struct Tmp(std::path::PathBuf);

impl Tmp {
    fn new() -> Self {
        Self(scratch())
    }

    fn path(&self, name: &str) -> std::path::PathBuf {
        self.0.join(name)
    }
}

impl Drop for Tmp {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

/// Mirrors `test_file_exists` in `test/cmocka/test_platform.c`.
#[test]
fn test_file_exists() {
    let tmp = Tmp::new();
    let path = tmp.path("exists.txt");
    assert!(!nix::file_exists(bytes(&path)));
    fs::write(&path, "x").unwrap();
    assert!(nix::file_exists(bytes(&path)));
    assert_eq!(nix::unlink(bytes(&path)), 0);
    assert!(!nix::file_exists(bytes(&path)));
}

/// Mirrors `test_unlink_missing_fails`.
#[test]
fn test_unlink_missing_fails() {
    let tmp = Tmp::new();
    let path = tmp.path("missing");
    assert_eq!(nix::unlink(bytes(&path)), -1);
}

/// Mirrors `test_mkdir_success_then_exists`.
#[test]
fn test_mkdir_success_then_exists() {
    let tmp = Tmp::new();
    let path = tmp.path("mk");
    assert_eq!(nix::mkdir(bytes(&path)), 0);
    assert_eq!(nix::mkdir(bytes(&path)), 1);
    assert_eq!(nix::rmdir(bytes(&path)), 0);
    assert!(!nix::file_exists(bytes(&path)));
}

/// Mirrors `test_mkdir_missing_parent_is_error`.
#[test]
fn test_mkdir_missing_parent_is_error() {
    let tmp = Tmp::new();
    let path = tmp.path("no_parent").join("child");
    assert_eq!(nix::mkdir(bytes(&path)), -1);
}

/// Mirrors `test_rename`.
#[test]
fn test_rename() {
    let tmp = Tmp::new();
    let a = tmp.path("a.txt");
    let b = tmp.path("b.txt");
    fs::write(&a, "payload").unwrap();
    assert_eq!(nix::rename(bytes(&a), bytes(&b)), 0);
    assert!(!nix::file_exists(bytes(&a)));
    assert!(nix::file_exists(bytes(&b)));
    assert_eq!(fs::read(&b).unwrap(), b"payload");
    assert_eq!(nix::rename(bytes(&a), bytes(&b)), -1);
}

/// Mirrors `test_safe_replace_with_backup`.
#[test]
fn test_safe_replace_with_backup() {
    let tmp = Tmp::new();
    let target = tmp.path("target");
    let from = tmp.path("from");
    let bak = tmp.path("bak");
    fs::write(&target, "old").unwrap();
    fs::write(&from, "new").unwrap();
    assert_eq!(
        nix::safe_replace(bytes(&target), bytes(&from), Some(bytes(&bak))),
        0
    );
    assert_eq!(fs::read(&target).unwrap(), b"new");
    assert_eq!(fs::read(&bak).unwrap(), b"old");
    assert!(!nix::file_exists(bytes(&from)));
}

/// Mirrors `test_safe_replace_without_backup`.
#[test]
fn test_safe_replace_without_backup() {
    let tmp = Tmp::new();
    let target = tmp.path("target");
    let from = tmp.path("from");
    let bak = tmp.path("bak");
    fs::write(&target, "old").unwrap();
    fs::write(&from, "new").unwrap();
    assert_eq!(nix::safe_replace(bytes(&target), bytes(&from), None), 0);
    assert_eq!(fs::read(&target).unwrap(), b"new");
    assert!(!nix::file_exists(bytes(&from)));
    assert!(!bak.exists());
}

/// Mirrors `test_opendir_readdir_closedir`.
#[test]
fn test_opendir_readdir_closedir() {
    let tmp = Tmp::new();
    let dir = tmp.path("list");
    let sub = dir.join("sub");
    fs::create_dir(&dir).unwrap();
    fs::create_dir(&sub).unwrap();
    fs::write(dir.join("l1.txt"), "1").unwrap();
    fs::write(dir.join("l2.txt"), "2").unwrap();

    let mut handle = nix::opendir(bytes(&dir)).unwrap();
    let mut ents: Vec<DirEnt> = Vec::new();
    while let Some(ent) = handle.read() {
        if ent.name == b"." || ent.name == b".." {
            continue;
        }
        ents.push(ent);
    }
    assert_eq!(ents.len(), 3);
    ents.sort_by(|a, b| a.name.cmp(&b.name));
    assert_eq!(ents[0].name, b"l1.txt");
    assert!(!ents[0].directory);
    assert_eq!(ents[1].name, b"l2.txt");
    assert!(!ents[1].directory);
    assert_eq!(ents[2].name, b"sub");
    assert!(ents[2].directory);

    assert!(nix::opendir(bytes(&tmp.path("no_such_dir"))).is_none());
}

/// Mirrors `test_get_abs_path_ptr_dot`.
#[test]
fn test_get_abs_path_ptr_dot() {
    let path = nix::abs_path(b".").unwrap();
    assert!(!path.is_empty());
    assert_eq!(path[0], b'/');
}

#[test]
fn copy_file_matches_bytes_and_refuses_an_existing_dest() {
    let tmp = Tmp::new();
    let src = tmp.path("src");
    let dst = tmp.path("dst");
    fs::write(&src, b"abc").unwrap();
    assert_eq!(nix::copy_file(bytes(&src), bytes(&dst)), 0);
    assert_eq!(fs::read(&dst).unwrap(), b"abc");
    assert_eq!(nix::copy_file(bytes(&src), bytes(&dst)), -1);
}

#[test]
fn glob_lists_the_matches() {
    let tmp = Tmp::new();
    fs::write(tmp.path("a.txt"), b"a").unwrap();
    fs::write(tmp.path("b.txt"), b"b").unwrap();
    let pattern = tmp.path("*.txt");
    let mut ents = nix::glob(bytes(&pattern)).unwrap();
    ents.sort_by(|a, b| a.path.cmp(&b.path));
    assert_eq!(ents.len(), 2);
    assert!(ents.iter().all(|e| !e.directory));
    assert!(ents[0].path.ends_with(b"a.txt"));
    assert!(ents[1].path.ends_with(b"b.txt"));
}

#[test]
fn sleepto_ns_past_returns_false() {
    assert!(!nix::sleepto_ns(0));
    assert!(!nix::sleepto_ns_fast(0));
}

#[test]
fn parent_dir_matches_dirname() {
    assert_eq!(nix::parent_dir(b"/usr/bin/obs"), b"/usr/bin");
    assert_eq!(nix::parent_dir(b"/obs"), b"/");
    assert_eq!(nix::parent_dir(b"obs"), b".");
    assert_eq!(nix::parent_dir(b"/"), b"/");
}

#[test]
fn cpuinfo_physical_cores_keeps_the_substring_match() {
    let text = "\
processor\t: 0
physical id\t: 10
cpu cores\t: 4

processor\t: 1
physical id\t: 1
cpu cores\t: 8
";
    assert_eq!(nix::cpuinfo_physical_cores(text, 99), 4);
    assert_eq!(nix::cpuinfo_physical_cores("processor\t: 0\n", 7), 7);
}

#[test]
fn freebsd_smp_cores_parses_the_second_line() {
    let text = b"\nFreeBSD/SMP: first\nFreeBSD/SMP: 2 package(s) x 4 core(s)\n";
    assert_eq!(nix::freebsd_smp_cores(text, 99), 8);
    assert_eq!(nix::freebsd_smp_cores(b"no smp here\n", 3), 3);
    let one = b"\nFreeBSD/SMP: only one line\n";
    assert_eq!(nix::freebsd_smp_cores(one, 5), 5);
}
