//! Tier 1: 1:1 port of `test_strtod` and `test_dtostr`
//! (`test/cmocka/test_platform.c`) and of `test_dtostr_exponent`
//! (`test/cmocka/test_dtostr_exponent.c`), through the C ABI shims.

use std::ffi::{CStr, CString};

use obs_c_oracle as _; // links the test allocator
use obs_util::ffi::platform_number::{os_dtostr, os_strtod};

fn strtod(s: &str) -> f64 {
    let c = CString::new(s).unwrap();
    // SAFETY: `c` is a valid NUL-terminated string that outlives the call.
    unsafe { os_strtod(c.as_ptr()) }
}

/// `os_dtostr` into a buffer of `size` bytes: the return value and, when it
/// succeeded, the string.
fn dtostr(value: f64, size: usize) -> (i32, Option<String>) {
    let mut buf = vec![0x7f_u8; size.max(1)];
    // SAFETY: `buf` is writable for `size` bytes.
    let ret = unsafe { os_dtostr(value, buf.as_mut_ptr().cast(), size) };
    let text = (ret >= 0).then(|| {
        CStr::from_bytes_until_nul(&buf)
            .unwrap()
            .to_str()
            .unwrap()
            .to_owned()
    });
    (ret, text)
}

#[test]
fn test_strtod() {
    assert_eq!(strtod("3.25"), 3.25);
    assert_eq!(strtod("-1.5"), -1.5);
    assert_eq!(strtod("1e2"), 100.0);
    assert_eq!(strtod("42"), 42.0);
    // unparsable input yields 0
    assert_eq!(strtod("abc"), 0.0);
}

#[test]
fn test_dtostr() {
    assert_eq!(dtostr(1.5, 64), (3, Some("1.5".into())));
    // ".0" is appended when there is no dot or exponent
    assert_eq!(dtostr(0.0, 64), (3, Some("0.0".into())));
    assert_eq!(dtostr(-2.25, 64), (5, Some("-2.25".into())));
    // too small a buffer
    assert_eq!(dtostr(1.5, 3).0, -1);
    // no room for the appended ".0"
    assert_eq!(dtostr(0.0, 3).0, -1);
}

#[test]
fn test_dtostr_exponent() {
    let cases: [(f64, &str); 10] = [
        (1.5e-5, "1.5e-5"),
        (1e-5, "1.0000000000000001e-5"),
        (1e17, "1e17"),
        (1.5e17, "1.5e17"),
        (1e-300, "1e-300"),
        (1e300, "1.0000000000000001e300"),
        (123_456_789_012_345_678.0, "1.2345678901234568e17"),
        (0.1, "0.10000000000000001"),
        (100.0, "100.0"),
        (1e16, "10000000000000000.0"),
    ];
    for (value, expected) in cases {
        let (len, text) = dtostr(value, 64);
        assert_eq!(text.as_deref(), Some(expected), "{value:e}");
        assert_eq!(len as usize, expected.len(), "{value:e}");
        assert_eq!(strtod(expected).to_bits(), value.to_bits(), "{value:e}");
    }
}
