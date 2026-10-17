//! Tier 3: the Rust C ABI shims for `os_strtod` and `os_dtostr` behave
//! exactly like the original `libobs/util/platform-number.c`, compiled as an
//! oracle.
//!
//! The tests run in the `C` locale (decimal point `.`), where the C locale
//! swap is a no-op; the one locale difference is documented on the shims.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use std::ffi::CString;

use obs_c_oracle::platform_number as c;
use obs_util::ffi::platform_number as rs;
use proptest::prelude::*;

/// `os_dtostr` from one side into a fresh buffer of `size` bytes: the return
/// value and the bytes up to the NUL (when it succeeded).
fn dtostr_with(
    f: unsafe extern "C" fn(f64, *mut core::ffi::c_char, usize) -> core::ffi::c_int,
    value: f64,
    size: usize,
) -> (i32, Option<Vec<u8>>) {
    let mut buf = vec![0x7f_u8; size.max(1)];
    // SAFETY: `buf` is writable for `size` bytes.
    let ret = unsafe { f(value, buf.as_mut_ptr().cast(), size) };
    let text = (ret >= 0).then(|| {
        let end = buf.iter().position(|&b| b == 0).expect("NUL-terminated");
        buf[..end].to_vec()
    });
    (ret, text)
}

fn assert_dtostr_same(value: f64, size: usize) {
    let o = dtostr_with(c::oracle_os_dtostr, value, size);
    let r = dtostr_with(rs::os_dtostr, value, size);
    assert_eq!(
        o,
        r,
        "os_dtostr({value:e} / {:#x}, {size})",
        value.to_bits()
    );
}

fn assert_strtod_same(s: &[u8]) {
    let cs = CString::new(s).unwrap();
    // SAFETY: `cs` is a valid NUL-terminated string that outlives both calls.
    let (o, r) = unsafe { (c::oracle_os_strtod(cs.as_ptr()), rs::os_strtod(cs.as_ptr())) };
    // NaN payloads and signs are the C library's choice; compare NaN-ness.
    if o.is_nan() || r.is_nan() {
        assert!(
            o.is_nan() && r.is_nan(),
            "os_strtod({s:?}): C {o:e}, Rust {r:e}"
        );
    } else {
        assert_eq!(
            o.to_bits(),
            r.to_bits(),
            "os_strtod({s:?}): C {o:e}, Rust {r:e}"
        );
    }
}

#[test]
fn dtostr_edge_values() {
    let values = [
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.1,
        1.5e-5,
        9.999e-5,
        1e-4,
        1e16,
        1e17,
        1e18,
        1e100,
        1e300,
        1e-300,
        f64::MIN_POSITIVE,
        f64::from_bits(1),
        f64::MAX,
        f64::MIN,
        f64::INFINITY,
        f64::NEG_INFINITY,
        f64::NAN,
    ];
    for v in values {
        for size in [0, 1, 2, 3, 4, 5, 6, 8, 16, 24, 25, 26, 32, 64] {
            assert_dtostr_same(v, size);
        }
    }
}

#[test]
fn strtod_edge_strings() {
    let inputs: &[&[u8]] = &[
        b"",
        b"abc",
        b"3.25",
        b"-1.5",
        b"+2",
        b"  \t7.5x",
        b"1e2",
        b"1e",
        b"1e+",
        b".5",
        b"5.",
        b".",
        b"-",
        b"inf",
        b"-Infinity",
        b"nan",
        b"0x1p4",
        b"0x",
        b"1,5",
        b"1e400",
        b"1e-400",
        // os_strtod reads at most 63 bytes.
        b"0.000000000000000000000000000000000000000000000000000000000000001",
        b"1234567890123456789012345678901234567890123456789012345678901234567890",
    ];
    for s in inputs {
        assert_strtod_same(s);
    }
}

proptest! {
    #[test]
    fn dtostr_matches_c_oracle(bits in any::<u64>(), size in 0usize..72) {
        assert_dtostr_same(f64::from_bits(bits), size);
    }

    #[test]
    fn dtostr_round_trips_through_strtod(bits in any::<u64>()) {
        let v = f64::from_bits(bits);
        prop_assume!(v.is_finite());
        let (_, text) = dtostr_with(rs::os_dtostr, v, 64);
        let text = text.expect("64 bytes always fit");
        let cs = CString::new(text).unwrap();
        // SAFETY: `cs` is a valid NUL-terminated string.
        let back = unsafe { rs::os_strtod(cs.as_ptr()) };
        prop_assert_eq!(back.to_bits(), v.to_bits());
    }

    #[test]
    fn strtod_matches_c_oracle(s in "[ +\\-0-9.eExXpPa-fA-FinNtTy,]{0,80}") {
        assert_strtod_same(s.as_bytes());
    }
}
