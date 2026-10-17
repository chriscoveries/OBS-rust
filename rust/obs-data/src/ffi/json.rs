//! Jansson 2.14 calls. Compact and pretty dumps go through `json_dumps` so
//! the text matches the C file. The crate does not link libjansson; libobs
//! already does, and `cargo test` gets the symbols from the vendored copy
//! in `obs-c-oracle`.

use core::ffi::{c_char, c_int, c_void};
use std::ffi::{CStr, CString};

const JSON_OBJECT: i32 = 0;
const JSON_ARRAY: i32 = 1;
const JSON_STRING: i32 = 2;
const JSON_INTEGER: i32 = 3;
const JSON_REAL: i32 = 4;
const JSON_TRUE: i32 = 5;
const JSON_FALSE: i32 = 6;
const JSON_NULL: i32 = 7;

const JSON_REJECT_DUPLICATES: usize = 0x1;
const JSON_COMPACT: usize = 0x20;
const JSON_PRESERVE_ORDER: usize = 0x100;
const JSON_INDENT_4: usize = 4;

const LOG_ERROR: c_int = 100;

const TEXT_LEN: usize = 160;
const SOURCE_LEN: usize = 80;

#[repr(C)]
struct JsonError {
    line: c_int,
    column: c_int,
    position: c_int,
    source: [c_char; SOURCE_LEN],
    text: [c_char; TEXT_LEN],
}

unsafe extern "C" {
    fn blog(level: c_int, fmt: *const c_char, ...);
    fn json_loads(input: *const c_char, flags: usize, error: *mut JsonError) -> *mut c_void;
    fn json_dumps(json: *mut c_void, flags: usize) -> *mut c_char;
    fn json_object() -> *mut c_void;
    fn json_array() -> *mut c_void;
    fn json_string(value: *const c_char) -> *mut c_void;
    fn json_integer(value: i64) -> *mut c_void;
    fn json_real(value: f64) -> *mut c_void;
    fn json_true() -> *mut c_void;
    fn json_false() -> *mut c_void;
    fn json_null() -> *mut c_void;
    fn json_object_set_new(object: *mut c_void, key: *const c_char, value: *mut c_void) -> c_int;
    fn json_array_append_new(array: *mut c_void, value: *mut c_void) -> c_int;
    fn json_object_iter(object: *mut c_void) -> *mut c_void;
    fn json_object_iter_next(object: *mut c_void, iter: *mut c_void) -> *mut c_void;
    fn json_object_iter_key(iter: *mut c_void) -> *const c_char;
    fn json_object_iter_value(iter: *mut c_void) -> *mut c_void;
    fn json_array_size(array: *mut c_void) -> usize;
    fn json_array_get(array: *mut c_void, index: usize) -> *mut c_void;
    fn json_string_value(string: *mut c_void) -> *const c_char;
    fn json_integer_value(integer: *mut c_void) -> i64;
    fn json_real_value(real: *mut c_void) -> f64;
    fn obs_data_json_decref(json: *mut c_void);
}

/// Owned JSON tree. Object keys stay in insertion order.
#[derive(Clone, Debug, PartialEq)]
pub enum J {
    Null,
    Bool(bool),
    Int(i64),
    Real(f64),
    String(String),
    Object(Vec<(String, J)>),
    Array(Vec<J>),
}

fn json_type(value: *mut c_void) -> i32 {
    unsafe { *(value as *const i32) }
}

fn c_string(text: &str) -> Option<CString> {
    CString::new(text).ok()
}

fn blog_parse(line: c_int, text: *const c_char) {
    let fmt = c"obs-data.c: [obs_data_create_from_json] Failed reading json string (%d): %s";
    unsafe { blog(LOG_ERROR, fmt.as_ptr(), line, text) }
}

/// `json_loads` with `JSON_REJECT_DUPLICATES` and without `JSON_DECODE_ANY`.
/// A failure is logged the way `obs_data_create_from_json` logs it.
pub fn loads(text: &str) -> Option<J> {
    let Ok(input) = CString::new(text) else {
        let msg = c"invalid string";
        blog_parse(1, msg.as_ptr());
        return None;
    };
    let mut error = JsonError {
        line: 0,
        column: 0,
        position: 0,
        source: [0; SOURCE_LEN],
        text: [0; TEXT_LEN],
    };
    let root = unsafe { json_loads(input.as_ptr(), JSON_REJECT_DUPLICATES, &mut error) };
    if root.is_null() {
        blog_parse(error.line, error.text.as_ptr());
        return None;
    }
    let value = convert(root);
    unsafe { obs_data_json_decref(root) };
    Some(value)
}

fn convert(value: *mut c_void) -> J {
    if value.is_null() {
        return J::Null;
    }
    match json_type(value) {
        JSON_OBJECT => {
            let mut pairs = Vec::new();
            let mut iter = unsafe { json_object_iter(value) };
            while !iter.is_null() {
                let key_ptr = unsafe { json_object_iter_key(iter) };
                let key = unsafe { CStr::from_ptr(key_ptr) }
                    .to_string_lossy()
                    .into_owned();
                let child = unsafe { json_object_iter_value(iter) };
                pairs.push((key, convert(child)));
                iter = unsafe { json_object_iter_next(value, iter) };
            }
            J::Object(pairs)
        }
        JSON_ARRAY => {
            let n = unsafe { json_array_size(value) };
            let mut items = Vec::with_capacity(n);
            for i in 0..n {
                items.push(convert(unsafe { json_array_get(value, i) }));
            }
            J::Array(items)
        }
        JSON_STRING => {
            let ptr = unsafe { json_string_value(value) };
            let text = if ptr.is_null() {
                String::new()
            } else {
                unsafe { CStr::from_ptr(ptr) }
                    .to_string_lossy()
                    .into_owned()
            };
            J::String(text)
        }
        JSON_INTEGER => J::Int(unsafe { json_integer_value(value) }),
        JSON_REAL => J::Real(unsafe { json_real_value(value) }),
        JSON_TRUE => J::Bool(true),
        JSON_FALSE => J::Bool(false),
        JSON_NULL => J::Null,
        _ => J::Null,
    }
}

/// `json_dumps` with `JSON_PRESERVE_ORDER` and either compact or indent 4.
pub fn dumps(value: &J, pretty: bool) -> Option<String> {
    let root = build(value)?;
    let flags = JSON_PRESERVE_ORDER | if pretty { JSON_INDENT_4 } else { JSON_COMPACT };
    let ptr = unsafe { json_dumps(root, flags) };
    unsafe { obs_data_json_decref(root) };
    if ptr.is_null() {
        return None;
    }
    let text = unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned();
    unsafe { libc::free(ptr as *mut c_void) };
    Some(text)
}

fn build(value: &J) -> Option<*mut c_void> {
    let ptr = match value {
        J::Null => unsafe { json_null() },
        J::Bool(true) => unsafe { json_true() },
        J::Bool(false) => unsafe { json_false() },
        J::Int(v) => unsafe { json_integer(*v) },
        J::Real(v) => unsafe { json_real(*v) },
        J::String(s) => {
            let c = c_string(s)?;
            unsafe { json_string(c.as_ptr()) }
        }
        J::Array(items) => {
            let array = unsafe { json_array() };
            if array.is_null() {
                return None;
            }
            for item in items {
                let Some(child) = build(item) else {
                    unsafe { obs_data_json_decref(array) };
                    return None;
                };
                if unsafe { json_array_append_new(array, child) } != 0 {
                    unsafe { obs_data_json_decref(child) };
                    unsafe { obs_data_json_decref(array) };
                    return None;
                }
            }
            array
        }
        J::Object(pairs) => {
            let object = unsafe { json_object() };
            if object.is_null() {
                return None;
            }
            for (key, child) in pairs {
                let key_c = match c_string(key) {
                    Some(k) => k,
                    None => {
                        unsafe { obs_data_json_decref(object) };
                        return None;
                    }
                };
                let child = match build(child) {
                    Some(c) => c,
                    None => {
                        unsafe { obs_data_json_decref(object) };
                        return None;
                    }
                };
                if unsafe { json_object_set_new(object, key_c.as_ptr(), child) } != 0 {
                    unsafe { obs_data_json_decref(child) };
                    unsafe { obs_data_json_decref(object) };
                    return None;
                }
            }
            object
        }
    };
    if ptr.is_null() { None } else { Some(ptr) }
}
