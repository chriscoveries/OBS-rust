//! Differential INI parsing, including arbitrary non-UTF-8 and malformed
//! input. C strings truncate at the first NUL on both sides. Null handles,
//! file I/O and null section/key arguments are not generated. Section order
//! and lookups for names derived from input lines are compared, not private
//! config layouts. No byte values are excluded. Duplicate section names and
//! keys are compared too: the port models C's uthash bucket expansion
//! (obs-rust/obs-studio#681).

#![no_main]

use core::ffi::{CStr, c_char};
use std::collections::BTreeSet;
use std::ffi::CString;

use libfuzzer_sys::fuzz_target;
use obs_c_oracle::config_file as c;
use obs_util::{config_file::Config, ffi::config_file as rs};

unsafe fn bytes(p: *const c_char) -> Option<Vec<u8>> {
    if p.is_null() {
        None
    } else {
        // SAFETY: caller supplies a config-owned NUL-terminated string.
        Some(unsafe { CStr::from_ptr(p) }.to_bytes().to_vec())
    }
}

fuzz_target!(|input: &[u8]| {
    let end = input.iter().position(|&b| b == 0).unwrap_or(input.len());
    let input = &input[..end];
    let text = CString::new(input).unwrap();
    let core = Config::open_string(input);
    let mut oracle = core::ptr::null_mut();
    let mut rust = core::ptr::null_mut();
    let mut keys = BTreeSet::from([Vec::new(), b"key".to_vec(), b"missing".to_vec()]);
    for part in input.split(|b| matches!(b, b'\n' | b'\r' | b'[' | b']' | b'=')) {
        let start = part
            .iter()
            .position(|b| !matches!(b, b' ' | b'\t'))
            .unwrap_or(part.len());
        keys.insert(part[start..].to_vec());
    }
    for line in input.split(|b| matches!(b, b'\n' | b'\r')) {
        if let Some(eq) = line.iter().position(|&b| b == b'=') {
            // Whole text before `=`, so keys containing `[` or `]` are looked up.
            let name = &line[..eq];
            let start = name
                .iter()
                .position(|b| !matches!(b, b' ' | b'\t'))
                .unwrap_or(name.len());
            keys.insert(name[start..].to_vec());
        }
    }
    // SAFETY: out-pointers are valid and text is NUL-terminated. Successful
    // opens yield live configs; all query strings outlive their calls, and
    // returned strings are copied before closing each config exactly once.
    unsafe {
        let status = c::oracle_config_open_string(&mut oracle, text.as_ptr());
        assert_eq!(rs::config_open_string(&mut rust, text.as_ptr()), status);
        assert_eq!(status, 0);
        let count = c::oracle_config_num_sections(oracle);
        assert_eq!(rs::config_num_sections(rust), count);
        assert_eq!(core.num_sections(), count);
        let mut sections = Vec::new();
        for index in 0..count {
            let section = bytes(c::oracle_config_get_section(oracle, index));
            assert_eq!(bytes(rs::config_get_section(rust, index)), section);
            assert_eq!(core.get_section(index), section);
            sections.push(section.unwrap());
        }
        for section in &sections {
            let section = CString::new(section.as_slice()).unwrap();
            for key in &keys {
                let key = CString::new(key.as_slice()).unwrap();
                let expected = bytes(c::oracle_config_get_string(
                    oracle,
                    section.as_ptr(),
                    key.as_ptr(),
                ));
                assert_eq!(
                    bytes(rs::config_get_string(rust, section.as_ptr(), key.as_ptr())),
                    expected,
                    "section {section:?} key {key:?}"
                );
                assert_eq!(
                    core.get_string(section.as_bytes(), key.as_bytes()),
                    expected
                );
            }
        }
        c::oracle_config_close(oracle);
        rs::config_close(rust);
    }
});
