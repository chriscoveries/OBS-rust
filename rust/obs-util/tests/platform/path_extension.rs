//! Tier 1: safe-core tests. `os_get_path_extension_test` mirrors
//! `test/cmocka/test_os_path.c` case for case.

use obs_c_oracle as _; // links the test allocator
use obs_util::path_extension::path_extension;

#[test]
fn os_get_path_extension_test() {
    assert_eq!(path_extension(b"/home/user/a.txt"), Some(&b".txt"[..]));
    assert_eq!(
        path_extension(b"C:\\Users\\user\\Documents\\video.mp4"),
        Some(&b".mp4"[..])
    );
    assert_eq!(path_extension(b"./\\"), None);
    assert_eq!(path_extension(b".\\/"), None);
    assert_eq!(path_extension(b"/.\\"), None);
    assert_eq!(path_extension(b"\\./"), None);
    assert_eq!(path_extension(b""), None);
    assert_eq!(path_extension(b"/\\."), Some(&b"."[..]));
    assert_eq!(path_extension(b"\\/."), Some(&b"."[..]));
}

#[test]
fn multiple_dots_use_the_last() {
    assert_eq!(path_extension(b"a.tar.gz"), Some(&b".gz"[..]));
}

#[test]
fn no_extension() {
    assert_eq!(path_extension(b"noext"), None);
}

#[test]
fn dot_in_directory_is_not_an_extension() {
    assert_eq!(path_extension(b"dir.d/file"), None);
}

#[test]
fn leading_dot_is_an_extension() {
    assert_eq!(path_extension(b".hidden"), Some(&b".hidden"[..]));
}

#[test]
fn trailing_dot_gives_dot() {
    assert_eq!(path_extension(b"file."), Some(&b"."[..]));
}
