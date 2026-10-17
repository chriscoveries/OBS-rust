//! Tier 3: the Rust C ABI shim behaves exactly like the original C,
//! compiled as an oracle.
//!
//! Intentional difference from C: a NULL `path` returns NULL in Rust; in C
//! it calls `strlen(NULL)` (undefined behavior). Not generated here.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use std::ffi::CString;

use obs_c_oracle::path_extension as c;
use obs_util::ffi::path_extension as rs;
use proptest::prelude::*;

/// Offset of `p` from `start`, or `None` for null.
fn offset(start: *const core::ffi::c_char, p: *const core::ffi::c_char) -> Option<isize> {
    if p.is_null() {
        None
    } else {
        // SAFETY: both pointers lie within the same live CString allocation.
        Some(unsafe { p.offset_from(start) })
    }
}

/// Tier 2: the shim returns pointers into the input for the C test cases.
#[test]
fn shim_returns_pointers_into_input() {
    let cases: [(&[u8], Option<isize>); 9] = [
        (b"/home/user/a.txt", Some(12)),
        (b"C:\\Users\\user\\Documents\\video.mp4", Some(29)),
        (b"./\\", None),
        (b".\\/", None),
        (b"/.\\", None),
        (b"\\./", None),
        (b"", None),
        (b"/\\.", Some(2)),
        (b"\\/.", Some(2)),
    ];
    for (input, expected) in cases {
        let s = CString::new(input).unwrap();
        // SAFETY: `s` is a valid NUL-terminated string that outlives the call.
        let got = unsafe { rs::os_get_path_extension(s.as_ptr()) };
        assert_eq!(offset(s.as_ptr(), got), expected, "{input:?}");
    }
}

proptest! {
    #[test]
    fn matches_c_oracle(
        bytes in prop::collection::vec(
            prop::sample::select(vec![b'a', b'x', b'.', b'/', b'\\', 0xC3, 0xA9]),
            0..48,
        )
    ) {
        let s = CString::new(bytes).unwrap();
        // SAFETY: `s` is a valid NUL-terminated string that outlives both calls.
        let (ours, theirs) = unsafe {
            (
                rs::os_get_path_extension(s.as_ptr()),
                c::oracle_os_get_path_extension(s.as_ptr()),
            )
        };
        prop_assert_eq!(offset(s.as_ptr(), ours), offset(s.as_ptr(), theirs));
    }
}
