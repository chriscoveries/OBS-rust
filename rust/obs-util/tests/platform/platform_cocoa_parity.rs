//! Tier 3: the Rust C ABI shim matches `libobs/util/platform-cocoa.m`.
//!
//! Intentional differences, excluded here:
//! - `os_cpu_usage_info` and `os_inhibit_t` are `Box` values.
//! - Core-count and total-memory caches take a mutex. The stored values
//!   match the C statics.
//! - A null `os_get_proc_memory_usage` returns false. C writes through it.
//! - A null `cfstr_copy_dstr` dstr pointer returns false. C writes through it.
//! - `cfstr_copy_dstr`'s slow path zeros the gap past the first NUL. C leaves
//!   those bytes uninitialized. Compared: the C string and `len`.
//! - `cfstr_copy_cstr`'s slow path is sized to the C string. C sizes it to
//!   the encoding maximum. Compared with the C string, not the allocation.
//! - An empty inhibit reason is `CFStringCreateWithCString("")`. C uses
//!   `CFStringCreateCopy(CFSTR(""))`. The text is empty either way, and
//!   these tests do not read the private `CFStringRef`.
//! - A null inhibit reason is passed to `CFStringCreateWithCString`. That
//!   call faults on current macOS, in C and here, so it is not invoked.
//! - A null `UTF8String` becomes an empty base path. C would `memcpy` it.
//!
//! CPU samples, free space, and process memory move between the two calls,
//! so those are checked for shape, not equality. High performance is begun
//! and ended. Sleep inhibit is created and destroyed without turning it on,
//! so a sandbox that refuses `IOPMAssertionCreateWithName` does not fail
//! the suite and no assertion is left active.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

#![cfg(target_os = "macos")]

use core::ffi::{CStr, c_char, c_void};
use std::ffi::CString;
use std::ptr;

use obs_c_oracle::platform_cocoa as c;
use obs_util::ffi::bmem::bfree;
use obs_util::ffi::dstr::dstr;
use obs_util::ffi::platform_cocoa as rs;

const CF_UTF8: u32 = 0x0800_0100;

#[link(name = "CoreFoundation", kind = "framework")]
unsafe extern "C" {
    fn CFStringCreateWithCString(
        alloc: *const c_void,
        c_str: *const c_char,
        encoding: u32,
    ) -> *const c_void;
    fn CFStringCreateWithCharacters(
        alloc: *const c_void,
        chars: *const u16,
        num_chars: isize,
    ) -> *const c_void;
    fn CFRelease(cf: *const c_void);
}

fn cstr_bytes(p: *const c_char) -> Option<Vec<u8>> {
    if p.is_null() {
        return None;
    }
    // SAFETY: `p` is a live C string from a path or cfstr helper.
    Some(unsafe { CStr::from_ptr(p) }.to_bytes().to_vec())
}

fn free_str(p: *mut c_char) {
    if !p.is_null() {
        // SAFETY: `p` came from `bmalloc` in the shim or the oracle.
        unsafe { bfree(p.cast()) };
    }
}

fn path_pair(name: *const c_char, which: Which) -> (i32, Vec<u8>, i32, Vec<u8>) {
    let mut rs_buf = [0u8; 1024];
    let mut c_buf = [0u8; 1024];
    // SAFETY: both buffers are 1024 bytes. `name` is null or a live C string.
    let (rs_n, c_n) = unsafe {
        match which {
            Which::Config => (
                rs::os_get_config_path(rs_buf.as_mut_ptr().cast(), rs_buf.len(), name),
                c::oracle_os_get_config_path(c_buf.as_mut_ptr().cast(), c_buf.len(), name),
            ),
            Which::Program => (
                rs::os_get_program_data_path(rs_buf.as_mut_ptr().cast(), rs_buf.len(), name),
                c::oracle_os_get_program_data_path(c_buf.as_mut_ptr().cast(), c_buf.len(), name),
            ),
        }
    };
    assert!(rs_n > 0 && c_n > 0);
    let rs_path = cstr_bytes(rs_buf.as_ptr().cast()).unwrap();
    let c_path = cstr_bytes(c_buf.as_ptr().cast()).unwrap();
    (rs_n, rs_path, c_n, c_path)
}

enum Which {
    Config,
    Program,
}

fn assert_paths(name: *const c_char, which: Which) {
    let (rs_n, rs_path, c_n, c_path) = path_pair(name, which);
    assert_eq!(rs_n, c_n);
    assert_eq!(rs_path, c_path);
    assert_eq!(rs_path.len(), rs_n as usize);
}

#[test]
fn config_and_program_paths_match() {
    let name = CString::new("obs-cocoa-test").unwrap();
    let empty = CString::new("").unwrap();
    assert_paths(ptr::null(), Which::Config);
    assert_paths(empty.as_ptr(), Which::Config);
    assert_paths(name.as_ptr(), Which::Config);
    assert_paths(ptr::null(), Which::Program);
    assert_paths(name.as_ptr(), Which::Program);

    // SAFETY: the names are live C strings. Results are bmalloc'd.
    unsafe {
        for name in [ptr::null(), empty.as_ptr(), name.as_ptr()] {
            let rs_ptr = rs::os_get_config_path_ptr(name);
            let c_ptr = c::oracle_os_get_config_path_ptr(name);
            assert_eq!(cstr_bytes(rs_ptr), cstr_bytes(c_ptr));
            free_str(rs_ptr);
            free_str(c_ptr);

            let rs_ptr = rs::os_get_program_data_path_ptr(name);
            let c_ptr = c::oracle_os_get_program_data_path_ptr(name);
            assert_eq!(cstr_bytes(rs_ptr), cstr_bytes(c_ptr));
            free_str(rs_ptr);
            free_str(c_ptr);
        }

        let rs_n = rs::os_get_config_path(ptr::null_mut(), 0, ptr::null());
        let c_n = c::oracle_os_get_config_path(ptr::null_mut(), 0, ptr::null());
        assert_eq!(rs_n, c_n);
        assert!(rs_n > 0);
    }
}

proptest::proptest! {
    #[test]
    fn config_name_suffix_matches(suffix in "[A-Za-z0-9]{0,24}") {
        let name = CString::new(suffix).unwrap();
        assert_paths(name.as_ptr(), Which::Config);
        // SAFETY: `name` is a live C string. Results are bmalloc'd.
        unsafe {
            let rs_ptr = rs::os_get_config_path_ptr(name.as_ptr());
            let c_ptr = c::oracle_os_get_config_path_ptr(name.as_ptr());
            assert_eq!(cstr_bytes(rs_ptr), cstr_bytes(c_ptr));
            free_str(rs_ptr);
            free_str(c_ptr);
        }
    }
}

#[test]
fn executable_path_matches() {
    let name = CString::new("obs-cocoa-name").unwrap();
    let empty = CString::new("").unwrap();
    // SAFETY: the names are live C strings. Results are bmalloc'd.
    unsafe {
        for name in [ptr::null(), empty.as_ptr(), name.as_ptr()] {
            let rs_ptr = rs::os_get_executable_path_ptr(name);
            let c_ptr = c::oracle_os_get_executable_path_ptr(name);
            assert_eq!(cstr_bytes(rs_ptr), cstr_bytes(c_ptr));
            assert!(cstr_bytes(rs_ptr).is_some());
            free_str(rs_ptr);
            free_str(c_ptr);
        }
    }
}

#[test]
fn clock_cores_and_total_memory_match() {
    // SAFETY: these calls take no pointers.
    unsafe {
        let rs_a = rs::os_gettime_ns();
        let c_a = c::oracle_os_gettime_ns();
        let rs_b = rs::os_gettime_ns();
        let c_b = c::oracle_os_gettime_ns();
        assert!(rs_a > 0 && c_a > 0);
        assert!(rs_b >= rs_a && c_b >= c_a);
        let gap = rs_a.abs_diff(c_a);
        assert!(gap < 1_000_000_000, "clocks diverged by {gap} ns");

        assert_eq!(
            rs::os_get_physical_cores(),
            c::oracle_os_get_physical_cores()
        );
        assert_eq!(rs::os_get_logical_cores(), c::oracle_os_get_logical_cores());
        assert!(rs::os_get_physical_cores() > 0);
        assert_eq!(
            rs::os_get_emulation_status(),
            c::oracle_os_get_emulation_status()
        );
        assert_eq!(
            rs::os_get_sys_total_size(),
            c::oracle_os_get_sys_total_size()
        );
        assert!(rs::os_get_sys_total_size() > 0);
        assert!(rs::os_get_sys_free_size() > 0);
        assert!(c::oracle_os_get_sys_free_size() > 0);
    }
}

#[test]
fn free_space_null_is_zero_and_root_is_positive() {
    // SAFETY: the root path is a literal. Null is the empty case.
    unsafe {
        assert_eq!(rs::os_get_free_space(ptr::null()), 0);
        assert_eq!(c::oracle_os_get_free_space(ptr::null()), 0);
        assert_eq!(rs::os_get_free_disk_space(ptr::null()), 0);
        assert_eq!(c::oracle_os_get_free_disk_space(ptr::null()), 0);
        let root = c"/";
        assert!(rs::os_get_free_space(root.as_ptr()) > 0);
        assert!(c::oracle_os_get_free_space(root.as_ptr()) > 0);
    }
}

#[test]
fn cpu_and_proc_memory_are_well_formed() {
    // SAFETY: null queries are the documented no-op. Live handles are destroyed.
    unsafe {
        assert_eq!(rs::os_cpu_usage_info_query(ptr::null_mut()), 0.0);
        assert_eq!(c::oracle_os_cpu_usage_info_query(ptr::null_mut()), 0.0);
        rs::os_cpu_usage_info_destroy(ptr::null_mut());
        c::oracle_os_cpu_usage_info_destroy(ptr::null_mut());

        let rs_info = rs::os_cpu_usage_info_start();
        let c_info = c::oracle_os_cpu_usage_info_start();
        assert!(!rs_info.is_null() && !c_info.is_null());
        assert!(!rs::os_cpu_usage_info_query(rs_info).is_nan());
        assert!(!c::oracle_os_cpu_usage_info_query(c_info).is_nan());
        rs::os_cpu_usage_info_destroy(rs_info);
        c::oracle_os_cpu_usage_info_destroy(c_info);

        assert!(!rs::os_get_proc_memory_usage(ptr::null_mut()));
        let mut rs_usage = c::OracleProcMem {
            resident_size: 0,
            virtual_size: 0,
        };
        let mut c_usage = c::OracleProcMem {
            resident_size: 0,
            virtual_size: 0,
        };
        // The oracle struct and the shim struct are the same layout.
        // The shim takes `os_proc_memory_usage`. Cast through the pointer.
        assert!(rs::os_get_proc_memory_usage(
            ptr::addr_of_mut!(rs_usage).cast()
        ));
        assert!(c::oracle_os_get_proc_memory_usage(&mut c_usage));
        assert!(rs_usage.resident_size > 0 && c_usage.resident_size > 0);
        assert!(rs_usage.virtual_size > 0 && c_usage.virtual_size > 0);
        assert!(rs::os_get_proc_resident_size() > 0);
        assert!(c::oracle_os_get_proc_resident_size() > 0);
        assert!(rs::os_get_proc_virtual_size() > 0);
        assert!(c::oracle_os_get_proc_virtual_size() > 0);
    }
}

#[test]
fn high_performance_and_inhibit_smoke() {
    let reason = CString::new("obs-cocoa-test").unwrap();
    // SAFETY: `reason` is a live C string. Tokens and inhibit objects are ended.
    unsafe {
        let rs_token = rs::os_request_high_performance(reason.as_ptr());
        assert!(!rs_token.is_null());
        rs::os_end_high_performance(rs_token);
        rs::os_end_high_performance(ptr::null_mut());

        let c_token = c::oracle_os_request_high_performance(reason.as_ptr());
        assert!(!c_token.is_null());
        c::oracle_os_end_high_performance(c_token);
        c::oracle_os_end_high_performance(ptr::null_mut());

        assert!(!rs::os_inhibit_sleep_set_active(ptr::null_mut(), true));
        assert!(!c::oracle_os_inhibit_sleep_set_active(
            ptr::null_mut(),
            true
        ));
        rs::os_inhibit_sleep_destroy(ptr::null_mut());
        c::oracle_os_inhibit_sleep_destroy(ptr::null_mut());

        let rs_info = rs::os_inhibit_sleep_create(reason.as_ptr());
        let c_info = c::oracle_os_inhibit_sleep_create(reason.as_ptr());
        assert!(!rs_info.is_null() && !c_info.is_null());
        assert!(!rs::os_inhibit_sleep_set_active(rs_info, false));
        assert!(!c::oracle_os_inhibit_sleep_set_active(c_info, false));
        rs::os_inhibit_sleep_destroy(rs_info);
        c::oracle_os_inhibit_sleep_destroy(c_info);
    }
}

fn empty_dstr() -> dstr {
    dstr {
        array: ptr::null_mut(),
        len: 0,
        capacity: 0,
    }
}

fn dstr_text(d: &dstr) -> Vec<u8> {
    cstr_bytes(d.array).unwrap_or_default()
}

fn release(cf: *const c_void) {
    if !cf.is_null() {
        // SAFETY: `cf` came from `CFStringCreate*`.
        unsafe { CFRelease(cf) };
    }
}

#[test]
fn cfstr_null_and_roundtrip_match() {
    // SAFETY: null CFString and null dstr are the documented edge cases.
    // Live strings are released. Copied buffers are bmalloc'd.
    unsafe {
        assert!(rs::cfstr_copy_cstr(ptr::null(), CF_UTF8).is_null());
        assert!(c::oracle_cfstr_copy_cstr(ptr::null(), CF_UTF8).is_null());

        let mut rs_d = empty_dstr();
        let mut c_d = c::OracleDstr {
            array: ptr::null_mut(),
            len: 0,
            capacity: 0,
        };
        assert!(!rs::cfstr_copy_dstr(ptr::null(), CF_UTF8, &mut rs_d));
        assert!(!c::oracle_cfstr_copy_dstr(ptr::null(), CF_UTF8, &mut c_d));
        assert!(!rs::cfstr_copy_dstr(ptr::null(), CF_UTF8, ptr::null_mut()));

        let text = c"obs-cocoa";
        let cf = CFStringCreateWithCString(ptr::null(), text.as_ptr(), CF_UTF8);
        assert!(!cf.is_null());
        let rs_owned = rs::cfstr_copy_cstr(cf, CF_UTF8);
        let c_owned = c::oracle_cfstr_copy_cstr(cf, CF_UTF8);
        assert_eq!(cstr_bytes(rs_owned), cstr_bytes(c_owned));
        assert_eq!(cstr_bytes(rs_owned).as_deref(), Some(&b"obs-cocoa"[..]));
        free_str(rs_owned);
        free_str(c_owned);

        assert!(rs::cfstr_copy_dstr(cf, CF_UTF8, &mut rs_d));
        assert!(c::oracle_cfstr_copy_dstr(cf, CF_UTF8, &mut c_d));
        assert_eq!(dstr_text(&rs_d), dstr_text_oracle(&c_d));
        assert_eq!(rs_d.len, c_d.len);
        assert_eq!(dstr_text(&rs_d), b"obs-cocoa");
        free_str(rs_d.array);
        free_str(c_d.array);
        release(cf);

        let accent = [0x00E9u16];
        let cf = CFStringCreateWithCharacters(ptr::null(), accent.as_ptr(), 1);
        assert!(!cf.is_null());
        let mut rs_d = empty_dstr();
        let mut c_d = c::OracleDstr {
            array: ptr::null_mut(),
            len: 0,
            capacity: 0,
        };
        assert!(rs::cfstr_copy_dstr(cf, CF_UTF8, &mut rs_d));
        assert!(c::oracle_cfstr_copy_dstr(cf, CF_UTF8, &mut c_d));
        assert_eq!(dstr_text(&rs_d), dstr_text_oracle(&c_d));
        assert_eq!(rs_d.len, c_d.len);
        assert_eq!(dstr_text(&rs_d), &[0xC3, 0xA9]);
        assert!(rs_d.len >= dstr_text(&rs_d).len());
        let rs_owned = rs::cfstr_copy_cstr(cf, CF_UTF8);
        let c_owned = c::oracle_cfstr_copy_cstr(cf, CF_UTF8);
        assert_eq!(cstr_bytes(rs_owned), cstr_bytes(c_owned));
        assert_eq!(cstr_bytes(rs_owned).as_deref(), Some(&[0xC3, 0xA9][..]));
        free_str(rs_owned);
        free_str(c_owned);
        free_str(rs_d.array);
        free_str(c_d.array);
        release(cf);
    }
}

fn dstr_text_oracle(d: &c::OracleDstr) -> Vec<u8> {
    cstr_bytes(d.array).unwrap_or_default()
}
