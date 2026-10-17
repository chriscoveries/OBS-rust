//! C ABI for `libobs/obs-properties.h`.
//!
//! `obs_properties_t *` is an `Rc<RefCell<Props>>` pointer and
//! `obs_property_t *` an `Rc<RefCell<Prop>>` pointer. A set from
//! `obs_properties_create*` holds one strong count owned by C; a group takes
//! that count over. Properties are owned by their set; their pointers stay
//! valid until the property is removed or its set destroyed, as in C.
//! Callbacks are called with no `RefCell` borrowed, so they may look up,
//! change, add and remove properties.
//!
//! Each export is the C function of the same name. A null handle is a no-op
//! (or returns the C default) everywhere C checks it.
//!
//! Intentional differences from C, all on inputs where C crashes or leaks:
//! - a null name returns null (C calls `strlen(NULL)` in the hash lookup),
//!   and `obs_properties_get_parent(NULL)` returns null (C dereferences it);
//! - an `*_insert*` index past the end is ignored (C's `da_insert` asserts,
//!   and without asserts moves memory by a negative size);
//! - nothing leaks: a file-save path's filter and a replaced button URL are
//!   freed (#831);
//! - a set added as the content of two groups is shared, not freed twice.
#![allow(clippy::missing_safety_doc)]

use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char, c_int, c_longlong, c_void};
use std::ptr;
use std::rc::Rc;

use crate::props::{
    self, AddError, ClickedFn, Data, DestroyFn, Fps, Modified2Fn, ModifiedFn, PendingDestroy, Prop,
    PropRc, Props, PropsRc, Value, ty,
};

const LOG_WARNING: c_int = 200;

unsafe extern "C" {
    fn blog(level: c_int, fmt: *const c_char, ...);
    fn obs_obj_get_data(obj: *mut c_void) -> *mut c_void;
}

#[allow(non_camel_case_types)]
pub type media_frames_per_second = Fps;

/* ------------------------------------------------------------------------- */
/* handles */

/// A new strong reference to the set behind `ptr`, or `None` for null.
unsafe fn set(ptr: *mut c_void) -> Option<PropsRc> {
    if ptr.is_null() {
        return None;
    }
    let raw = ptr as *const RefCell<Props>;
    // SAFETY: a non-null handle is a live `Rc<RefCell<Props>>` pointer; the
    // extra count keeps the clone balanced.
    unsafe {
        Rc::increment_strong_count(raw);
        Some(Rc::from_raw(raw))
    }
}

/// A new strong reference to the property behind `ptr`, or `None`.
unsafe fn prop(ptr: *mut c_void) -> Option<PropRc> {
    if ptr.is_null() {
        return None;
    }
    let raw = ptr as *const RefCell<Prop>;
    // SAFETY: a non-null handle is a live `Rc<RefCell<Prop>>` pointer.
    unsafe {
        Rc::increment_strong_count(raw);
        Some(Rc::from_raw(raw))
    }
}

fn set_ptr(s: &PropsRc) -> *mut c_void {
    Rc::as_ptr(s) as *mut c_void
}

fn prop_ptr(p: &PropRc) -> *mut c_void {
    Rc::as_ptr(p) as *mut c_void
}

fn opt_prop_ptr(p: Option<PropRc>) -> *mut c_void {
    p.as_ref().map_or(ptr::null_mut(), prop_ptr)
}

unsafe fn cstr<'a>(s: *const c_char) -> Option<&'a CStr> {
    // SAFETY: the caller passes null or a NUL-terminated string.
    (!s.is_null()).then(|| unsafe { CStr::from_ptr(s) })
}

fn str_ptr(s: Option<&CString>) -> *const c_char {
    s.map_or(ptr::null(), |s| s.as_ptr())
}

fn log_exists(name: &CStr) {
    // SAFETY: a literal format with one `%s`, matched by `name`.
    unsafe { blog(LOG_WARNING, c"Property '%s' exists".as_ptr(), name.as_ptr()) };
}

fn run_destroys(list: Vec<PendingDestroy>) {
    for (param, f) in list {
        // SAFETY: the callback and param the caller registered together.
        unsafe { f(param as *mut c_void) };
    }
}

/// The type data of `p` when it is a property of type `kind`.
fn with_data<R>(p: *mut c_void, kind: i32, f: impl FnOnce(&mut Data) -> R) -> Option<R> {
    // SAFETY: `p` is null or a live property handle.
    let p = unsafe { prop(p) }?;
    let mut b = p.borrow_mut();
    (b.kind == kind).then(|| f(&mut b.data))
}

fn add_result(r: Result<PropRc, AddError>, name: &CStr) -> *mut c_void {
    match r {
        Ok(p) => prop_ptr(&p),
        Err(AddError::Exists) => {
            log_exists(name);
            ptr::null_mut()
        }
        Err(_) => ptr::null_mut(),
    }
}

unsafe fn add_simple(
    props: *mut c_void,
    name: *const c_char,
    desc: *const c_char,
    kind: i32,
    data: Data,
) -> *mut c_void {
    // SAFETY: handles and strings come from the C caller.
    let (Some(s), Some(n)) = (unsafe { set(props) }, unsafe { cstr(name) }) else {
        return ptr::null_mut();
    };
    add_result(props::add(&s, n, unsafe { cstr(desc) }, kind, data), n)
}

/* ------------------------------------------------------------------------- */
/* sets */

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_create() -> *mut c_void {
    Rc::into_raw(props::create()) as *mut c_void
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_create_param(
    param: *mut c_void,
    destroy: Option<DestroyFn>,
) -> *mut c_void {
    let s = props::create();
    run_destroys(
        props::set_param(&s, param as usize, destroy)
            .into_iter()
            .collect(),
    );
    Rc::into_raw(s) as *mut c_void
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_destroy(props: *mut c_void) {
    if props.is_null() {
        return;
    }
    // SAFETY: C owns one strong count of a set it created; this releases it.
    let s = unsafe { Rc::from_raw(props as *const RefCell<Props>) };
    let pending = props::destroy_list(&s);
    run_destroys(pending);
    drop(s);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_set_flags(props: *mut c_void, flags: u32) {
    if let Some(s) = unsafe { set(props) } {
        s.borrow_mut().flags = flags;
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_get_flags(props: *mut c_void) -> u32 {
    unsafe { set(props) }.map_or(0, |s| s.borrow().flags)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_set_param(
    props: *mut c_void,
    param: *mut c_void,
    destroy: Option<DestroyFn>,
) {
    if let Some(s) = unsafe { set(props) } {
        let old = props::set_param(&s, param as usize, destroy);
        drop(s);
        run_destroys(old.into_iter().collect());
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_get_param(props: *mut c_void) -> *mut c_void {
    unsafe { set(props) }.map_or(ptr::null_mut(), |s| s.borrow().param as *mut c_void)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_first(props: *mut c_void) -> *mut c_void {
    opt_prop_ptr(unsafe { set(props) }.and_then(|s| props::first(&s)))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_get(
    props: *mut c_void,
    property: *const c_char,
) -> *mut c_void {
    let (Some(s), Some(n)) = (unsafe { set(props) }, unsafe { cstr(property) }) else {
        return ptr::null_mut();
    };
    opt_prop_ptr(props::get(&s, n))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_get_parent(props: *mut c_void) -> *mut c_void {
    unsafe { set(props) }
        .and_then(|s| props::parent(&s))
        .map_or(ptr::null_mut(), |p| set_ptr(&p))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_remove_by_name(
    props: *mut c_void,
    property: *const c_char,
) {
    let (Some(s), Some(n)) = (unsafe { set(props) }, unsafe { cstr(property) }) else {
        return;
    };
    let pending = props::remove_by_name(&s, n);
    drop(s);
    run_destroys(pending);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_apply_settings(props: *mut c_void, settings: *mut c_void) {
    let Some(s) = (unsafe { set(props) }) else {
        return;
    };
    let order = props::apply_settings_order(&s);
    let sp = set_ptr(&s);
    for p in order {
        let (m, m2, priv_) = {
            let b = p.borrow();
            (b.modified, b.modified2, b.priv_)
        };
        // SAFETY: callbacks the plugin registered, with its own handles.
        unsafe {
            if let Some(f) = m {
                f(sp, prop_ptr(&p), settings);
            } else if let Some(f) = m2 {
                f(priv_ as *mut c_void, sp, prop_ptr(&p), settings);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* adding */

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_bool(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> *mut c_void {
    unsafe { add_simple(props, name, description, ty::BOOL, Data::None) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_int(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    min: c_int,
    max: c_int,
    step: c_int,
) -> *mut c_void {
    let d = Data::Int(props::number(min, max, step, props::NUMBER_SCROLLER));
    unsafe { add_simple(props, name, description, ty::INT, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_float(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    min: f64,
    max: f64,
    step: f64,
) -> *mut c_void {
    let d = Data::Float(props::number(min, max, step, props::NUMBER_SCROLLER));
    unsafe { add_simple(props, name, description, ty::FLOAT, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_int_slider(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    min: c_int,
    max: c_int,
    step: c_int,
) -> *mut c_void {
    let d = Data::Int(props::number(min, max, step, props::NUMBER_SLIDER));
    unsafe { add_simple(props, name, description, ty::INT, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_float_slider(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    min: f64,
    max: f64,
    step: f64,
) -> *mut c_void {
    let d = Data::Float(props::number(min, max, step, props::NUMBER_SLIDER));
    unsafe { add_simple(props, name, description, ty::FLOAT, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_text(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    type_: c_int,
) -> *mut c_void {
    let d = Data::Text {
        kind: type_,
        monospace: false,
        info_kind: props::TEXT_INFO_NORMAL,
        info_word_wrap: true,
    };
    unsafe { add_simple(props, name, description, ty::TEXT, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_path(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    type_: c_int,
    filter: *const c_char,
    default_path: *const c_char,
) -> *mut c_void {
    let keeps_filter = type_ == props::PATH_FILE || type_ == props::PATH_FILE_SAVE;
    let d = Data::Path {
        kind: type_,
        filter: if keeps_filter {
            props::dup(unsafe { cstr(filter) })
        } else {
            None
        },
        default_path: props::dup(unsafe { cstr(default_path) }),
    };
    unsafe { add_simple(props, name, description, ty::PATH, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_list(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    type_: c_int,
    format: c_int,
) -> *mut c_void {
    let (Some(s), Some(n)) = (unsafe { set(props) }, unsafe { cstr(name) }) else {
        return ptr::null_mut();
    };
    match props::add_list(&s, n, unsafe { cstr(description) }, type_, format) {
        Err(AddError::EditableNotString) => {
            // SAFETY: a literal format with one `%s`, matched by `n`.
            unsafe {
                blog(
                    LOG_WARNING,
                    c"List '%s', error: Editable combo boxes must be of the 'string' type".as_ptr(),
                    n.as_ptr(),
                );
            }
            ptr::null_mut()
        }
        r => add_result(r, n),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_color(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> *mut c_void {
    unsafe { add_simple(props, name, description, ty::COLOR, Data::None) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_color_alpha(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> *mut c_void {
    unsafe { add_simple(props, name, description, ty::COLOR_ALPHA, Data::None) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_button(
    props: *mut c_void,
    name: *const c_char,
    text: *const c_char,
    callback: Option<ClickedFn>,
) -> *mut c_void {
    unsafe { obs_properties_add_button2(props, name, text, callback, ptr::null_mut()) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_button2(
    props: *mut c_void,
    name: *const c_char,
    text: *const c_char,
    callback: Option<ClickedFn>,
    priv_: *mut c_void,
) -> *mut c_void {
    let d = Data::Button {
        callback,
        kind: props::BUTTON_DEFAULT,
        url: None,
    };
    let p = unsafe { add_simple(props, name, text, ty::BUTTON, d) };
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().priv_ = priv_ as usize;
    }
    p
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_font(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> *mut c_void {
    unsafe { add_simple(props, name, description, ty::FONT, Data::None) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_editable_list(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    type_: c_int,
    filter: *const c_char,
    default_path: *const c_char,
) -> *mut c_void {
    let d = Data::EditableList {
        kind: type_,
        filter: props::dup(unsafe { cstr(filter) }),
        default_path: props::dup(unsafe { cstr(default_path) }),
    };
    unsafe { add_simple(props, name, description, ty::EDITABLE_LIST, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_frame_rate(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> *mut c_void {
    let d = Data::FrameRate {
        options: Vec::new(),
        ranges: Vec::new(),
    };
    unsafe { add_simple(props, name, description, ty::FRAME_RATE, d) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_properties_add_group(
    props: *mut c_void,
    name: *const c_char,
    description: *const c_char,
    type_: c_int,
    group: *mut c_void,
) -> *mut c_void {
    let (Some(s), Some(n)) = (unsafe { set(props) }, unsafe { cstr(name) }) else {
        return ptr::null_mut();
    };
    let g = unsafe { set(group) };
    match props::add_group(
        &s,
        n,
        unsafe { cstr(description) },
        type_,
        g.as_ref(),
        log_exists,
    ) {
        Ok(p) => {
            // The group now holds the set; release the count C owned.
            // SAFETY: `group` was created by `obs_properties_create*`.
            drop(unsafe { Rc::from_raw(group as *const RefCell<Props>) });
            prop_ptr(&p)
        }
        Err(_) => ptr::null_mut(),
    }
}

/* ------------------------------------------------------------------------- */
/* common property fields */

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_next(p: *mut *mut c_void) -> bool {
    if p.is_null() {
        return false;
    }
    // SAFETY: `p` points to a property handle (or null).
    let cur = unsafe { *p };
    let Some(cur) = (unsafe { prop(cur) }) else {
        return false;
    };
    let next = opt_prop_ptr(props::next(&cur));
    // SAFETY: `p` is writable, as above.
    unsafe { *p = next };
    !next.is_null()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_modified_callback(
    p: *mut c_void,
    modified: Option<ModifiedFn>,
) {
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().modified = modified;
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_modified_callback2(
    p: *mut c_void,
    modified: Option<Modified2Fn>,
    priv_: *mut c_void,
) {
    if let Some(p) = unsafe { prop(p) } {
        let mut b = p.borrow_mut();
        b.modified2 = modified;
        b.priv_ = priv_ as usize;
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_modified(p: *mut c_void, settings: *mut c_void) -> bool {
    let Some(pr) = (unsafe { prop(p) }) else {
        return false;
    };
    let (m, m2, priv_) = {
        let b = pr.borrow();
        (b.modified, b.modified2, b.priv_)
    };
    if m.is_none() && m2.is_none() {
        return false;
    }
    let top = props::owner(&pr).map(|o| props::topmost(&o));
    let tp = top.as_ref().map_or(ptr::null_mut(), set_ptr);
    // SAFETY: callbacks the plugin registered, with its own handles.
    unsafe {
        match (m, m2) {
            (Some(f), _) => f(tp, p, settings),
            (None, Some(f)) => f(priv_ as *mut c_void, tp, p, settings),
            (None, None) => false,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_button_clicked(p: *mut c_void, obj: *mut c_void) -> bool {
    let Some(pr) = (unsafe { prop(p) }) else {
        return false;
    };
    let (cb, priv_) = {
        let b = pr.borrow();
        match (&b.data, b.kind) {
            (Data::Button { callback, .. }, ty::BUTTON) => (*callback, b.priv_),
            _ => return false,
        }
    };
    let Some(cb) = cb else {
        return false;
    };
    let top = props::owner(&pr).map(|o| props::topmost(&o));
    let tp = top.as_ref().map_or(ptr::null_mut(), set_ptr);
    // SAFETY: `obj` is null or a libobs context; the callback is the
    // plugin's own.
    unsafe {
        let data = if priv_ != 0 {
            priv_ as *mut c_void
        } else {
            obs_obj_get_data(obj)
        };
        cb(tp, p, data)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_visible(p: *mut c_void, visible: bool) {
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().visible = visible;
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_enabled(p: *mut c_void, enabled: bool) {
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().enabled = enabled;
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_description(p: *mut c_void, description: *const c_char) {
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().desc = props::dup_nonempty(unsafe { cstr(description) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_set_long_description(
    p: *mut c_void,
    long_description: *const c_char,
) {
    if let Some(p) = unsafe { prop(p) } {
        p.borrow_mut().long_desc = props::dup_nonempty(unsafe { cstr(long_description) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_name(p: *mut c_void) -> *const c_char {
    unsafe { prop(p) }.map_or(ptr::null(), |p| p.borrow().name.as_ptr())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_description(p: *mut c_void) -> *const c_char {
    unsafe { prop(p) }.map_or(ptr::null(), |p| str_ptr(p.borrow().desc.as_ref()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_long_description(p: *mut c_void) -> *const c_char {
    unsafe { prop(p) }.map_or(ptr::null(), |p| str_ptr(p.borrow().long_desc.as_ref()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_get_type(p: *mut c_void) -> c_int {
    unsafe { prop(p) }.map_or(ty::INVALID, |p| p.borrow().kind)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_enabled(p: *mut c_void) -> bool {
    unsafe { prop(p) }.is_some_and(|p| p.borrow().enabled)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_visible(p: *mut c_void) -> bool {
    unsafe { prop(p) }.is_some_and(|p| p.borrow().visible)
}

/* ------------------------------------------------------------------------- */
/* numbers */

fn int_field<R>(p: *mut c_void, f: impl FnOnce(&mut props::Number<i32>) -> R) -> Option<R> {
    with_data(p, ty::INT, |d| match d {
        Data::Int(n) => Some(f(n)),
        _ => None,
    })
    .flatten()
}

fn float_field<R>(p: *mut c_void, f: impl FnOnce(&mut props::Number<f64>) -> R) -> Option<R> {
    with_data(p, ty::FLOAT, |d| match d {
        Data::Float(n) => Some(f(n)),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_min(p: *mut c_void) -> c_int {
    int_field(p, |n| n.min).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_max(p: *mut c_void) -> c_int {
    int_field(p, |n| n.max).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_step(p: *mut c_void) -> c_int {
    int_field(p, |n| n.step).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_type(p: *mut c_void) -> c_int {
    int_field(p, |n| n.kind).unwrap_or(props::NUMBER_SCROLLER)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_suffix(p: *mut c_void) -> *const c_char {
    int_field(p, |n| str_ptr(n.suffix.as_ref())).unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_min(p: *mut c_void) -> f64 {
    float_field(p, |n| n.min).unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_max(p: *mut c_void) -> f64 {
    float_field(p, |n| n.max).unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_step(p: *mut c_void) -> f64 {
    float_field(p, |n| n.step).unwrap_or(0.0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_type(p: *mut c_void) -> c_int {
    float_field(p, |n| n.kind).unwrap_or(props::NUMBER_SCROLLER)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_suffix(p: *mut c_void) -> *const c_char {
    float_field(p, |n| str_ptr(n.suffix.as_ref())).unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_set_limits(
    p: *mut c_void,
    min: c_int,
    max: c_int,
    step: c_int,
) {
    int_field(p, |n| {
        n.min = min;
        n.max = max;
        n.step = step;
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_set_limits(
    p: *mut c_void,
    min: f64,
    max: f64,
    step: f64,
) {
    float_field(p, |n| {
        n.min = min;
        n.max = max;
        n.step = step;
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_int_set_suffix(p: *mut c_void, suffix: *const c_char) {
    let s = props::dup(unsafe { cstr(suffix) });
    int_field(p, |n| n.suffix = s);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_float_set_suffix(p: *mut c_void, suffix: *const c_char) {
    let s = props::dup(unsafe { cstr(suffix) });
    float_field(p, |n| n.suffix = s);
}

/* ------------------------------------------------------------------------- */
/* text, path, editable list, button */

fn text<R>(
    p: *mut c_void,
    f: impl FnOnce(&mut i32, &mut bool, &mut i32, &mut bool) -> R,
) -> Option<R> {
    with_data(p, ty::TEXT, |d| match d {
        Data::Text {
            kind,
            monospace,
            info_kind,
            info_word_wrap,
        } => Some(f(kind, monospace, info_kind, info_word_wrap)),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_type(p: *mut c_void) -> c_int {
    text(p, |k, _, _, _| *k).unwrap_or(props::TEXT_DEFAULT)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_monospace(p: *mut c_void) -> bool {
    text(p, |_, m, _, _| *m).unwrap_or(false)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_info_type(p: *mut c_void) -> c_int {
    text(p, |_, _, i, _| *i).unwrap_or(props::TEXT_INFO_NORMAL)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_info_word_wrap(p: *mut c_void) -> bool {
    text(p, |_, _, _, w| *w).unwrap_or(true)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_set_monospace(p: *mut c_void, monospace: bool) {
    text(p, |_, m, _, _| *m = monospace);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_set_info_type(p: *mut c_void, type_: c_int) {
    text(p, |_, _, i, _| *i = type_);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_text_set_info_word_wrap(p: *mut c_void, word_wrap: bool) {
    text(p, |_, _, _, w| *w = word_wrap);
}

fn path<R>(
    p: *mut c_void,
    f: impl FnOnce(i32, Option<&CString>, Option<&CString>) -> R,
) -> Option<R> {
    with_data(p, ty::PATH, |d| match d {
        Data::Path {
            kind,
            filter,
            default_path,
        } => Some(f(*kind, filter.as_ref(), default_path.as_ref())),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_path_type(p: *mut c_void) -> c_int {
    path(p, |k, _, _| k).unwrap_or(props::PATH_DIRECTORY)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_path_filter(p: *mut c_void) -> *const c_char {
    path(p, |_, f, _| str_ptr(f)).unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_path_default_path(p: *mut c_void) -> *const c_char {
    path(p, |_, _, d| str_ptr(d)).unwrap_or(ptr::null())
}

fn editable<R>(
    p: *mut c_void,
    f: impl FnOnce(i32, Option<&CString>, Option<&CString>) -> R,
) -> Option<R> {
    with_data(p, ty::EDITABLE_LIST, |d| match d {
        Data::EditableList {
            kind,
            filter,
            default_path,
        } => Some(f(*kind, filter.as_ref(), default_path.as_ref())),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_editable_list_type(p: *mut c_void) -> c_int {
    editable(p, |k, _, _| k).unwrap_or(props::EDITABLE_LIST_TYPE_STRINGS)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_editable_list_filter(p: *mut c_void) -> *const c_char {
    editable(p, |_, f, _| str_ptr(f)).unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_editable_list_default_path(p: *mut c_void) -> *const c_char {
    editable(p, |_, _, d| str_ptr(d)).unwrap_or(ptr::null())
}

fn button<R>(p: *mut c_void, f: impl FnOnce(&mut i32, &mut Option<CString>) -> R) -> Option<R> {
    with_data(p, ty::BUTTON, |d| match d {
        Data::Button { kind, url, .. } => Some(f(kind, url)),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_button_set_type(p: *mut c_void, type_: c_int) {
    button(p, |k, _| *k = type_);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_button_set_url(p: *mut c_void, url: *mut c_char) {
    let u = props::dup(unsafe { cstr(url) });
    button(p, |_, cur| *cur = u);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_button_type(p: *mut c_void) -> c_int {
    button(p, |k, _| *k).unwrap_or(props::BUTTON_DEFAULT)
}

/// A non-button returns `""`, but a button without a URL returns null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_button_url(p: *mut c_void) -> *const c_char {
    button(p, |_, u| str_ptr(u.as_ref())).unwrap_or(c"".as_ptr())
}

/* ------------------------------------------------------------------------- */
/* lists */

fn list<R>(p: *mut c_void, f: impl FnOnce(&mut Vec<props::ListItem>, i32, i32) -> R) -> Option<R> {
    with_data(p, ty::LIST, |d| match d {
        Data::List {
            items,
            kind,
            format,
        } => Some(f(items, *kind, *format)),
        _ => None,
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_type(p: *mut c_void) -> c_int {
    list(p, |_, k, _| k).unwrap_or(props::COMBO_TYPE_INVALID)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_format(p: *mut c_void) -> c_int {
    list(p, |_, _, f| f).unwrap_or(props::COMBO_FORMAT_INVALID)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_clear(p: *mut c_void) {
    list(p, |items, _, _| items.clear());
}

unsafe fn list_add(p: *mut c_void, format: i32, name: *const c_char, value: Value) -> usize {
    unsafe { prop(p) }
        .and_then(|pr| props::list_add(&pr, format, unsafe { cstr(name) }, value))
        .unwrap_or(0)
}

unsafe fn list_insert(p: *mut c_void, idx: usize, format: i32, name: *const c_char, value: Value) {
    if let Some(pr) = unsafe { prop(p) } {
        props::list_insert(&pr, idx, format, unsafe { cstr(name) }, value);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_add_string(
    p: *mut c_void,
    name: *const c_char,
    val: *const c_char,
) -> usize {
    let v = Value::Str(props::dup(unsafe { cstr(val) }));
    unsafe { list_add(p, props::COMBO_FORMAT_STRING, name, v) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_add_int(
    p: *mut c_void,
    name: *const c_char,
    val: c_longlong,
) -> usize {
    unsafe { list_add(p, props::COMBO_FORMAT_INT, name, Value::Int(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_add_float(
    p: *mut c_void,
    name: *const c_char,
    val: f64,
) -> usize {
    unsafe { list_add(p, props::COMBO_FORMAT_FLOAT, name, Value::Float(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_add_bool(
    p: *mut c_void,
    name: *const c_char,
    val: bool,
) -> usize {
    unsafe { list_add(p, props::COMBO_FORMAT_BOOL, name, Value::Bool(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_insert_string(
    p: *mut c_void,
    idx: usize,
    name: *const c_char,
    val: *const c_char,
) {
    let v = Value::Str(props::dup(unsafe { cstr(val) }));
    unsafe { list_insert(p, idx, props::COMBO_FORMAT_STRING, name, v) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_insert_int(
    p: *mut c_void,
    idx: usize,
    name: *const c_char,
    val: c_longlong,
) {
    unsafe { list_insert(p, idx, props::COMBO_FORMAT_INT, name, Value::Int(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_insert_float(
    p: *mut c_void,
    idx: usize,
    name: *const c_char,
    val: f64,
) {
    unsafe { list_insert(p, idx, props::COMBO_FORMAT_FLOAT, name, Value::Float(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_insert_bool(
    p: *mut c_void,
    idx: usize,
    name: *const c_char,
    val: bool,
) {
    unsafe { list_insert(p, idx, props::COMBO_FORMAT_BOOL, name, Value::Bool(val)) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_disable(
    p: *mut c_void,
    idx: usize,
    disabled: bool,
) {
    list(p, |items, _, _| {
        if let Some(i) = items.get_mut(idx) {
            i.disabled = disabled;
        }
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_disabled(p: *mut c_void, idx: usize) -> bool {
    list(p, |items, _, _| items.get(idx).is_some_and(|i| i.disabled)).unwrap_or(false)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_remove(p: *mut c_void, idx: usize) {
    list(p, |items, _, _| {
        if idx < items.len() {
            items.remove(idx);
        }
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_count(p: *mut c_void) -> usize {
    list(p, |items, _, _| items.len()).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_name(p: *mut c_void, idx: usize) -> *const c_char {
    list(p, |items, _, _| {
        items
            .get(idx)
            .map_or(ptr::null(), |i| str_ptr(i.name.as_ref()))
    })
    .unwrap_or(ptr::null())
}

/// The item's value when the list has `format`.
fn item_value<R>(
    p: *mut c_void,
    idx: usize,
    format: i32,
    f: impl FnOnce(&Value) -> R,
) -> Option<R> {
    list(p, |items, _, fmt| {
        (fmt == format)
            .then(|| items.get(idx).map(|i| f(&i.value)))
            .flatten()
    })
    .flatten()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_string(
    p: *mut c_void,
    idx: usize,
) -> *const c_char {
    item_value(p, idx, props::COMBO_FORMAT_STRING, |v| match v {
        Value::Str(s) => str_ptr(s.as_ref()),
        _ => ptr::null(),
    })
    .unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_int(p: *mut c_void, idx: usize) -> c_longlong {
    item_value(p, idx, props::COMBO_FORMAT_INT, |v| match v {
        Value::Int(i) => *i,
        _ => 0,
    })
    .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_float(p: *mut c_void, idx: usize) -> f64 {
    item_value(p, idx, props::COMBO_FORMAT_FLOAT, |v| match v {
        Value::Float(d) => *d,
        _ => 0.0,
    })
    .unwrap_or(0.0)
}

/// C reads the union's `double` member; it is non-zero exactly when the
/// stored bool is true (the rest of the item is zeroed).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_list_item_bool(p: *mut c_void, idx: usize) -> bool {
    item_value(p, idx, props::COMBO_FORMAT_BOOL, |v| {
        matches!(v, Value::Bool(true))
    })
    .unwrap_or(false)
}

/* ------------------------------------------------------------------------- */
/* frame rate */

type Options = Vec<(Option<CString>, Option<CString>)>;
type Ranges = Vec<(Fps, Fps)>;

fn frame_rate<R>(p: *mut c_void, f: impl FnOnce(&mut Options, &mut Ranges) -> R) -> Option<R> {
    with_data(p, ty::FRAME_RATE, |d| match d {
        Data::FrameRate { options, ranges } => Some(f(options, ranges)),
        _ => None,
    })
    .flatten()
}

const DARRAY_INVALID: usize = usize::MAX;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_clear(p: *mut c_void) {
    frame_rate(p, |o, r| {
        o.clear();
        r.clear();
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_options_clear(p: *mut c_void) {
    frame_rate(p, |o, _| o.clear());
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_ranges_clear(p: *mut c_void) {
    frame_rate(p, |_, r| r.clear());
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_option_add(
    p: *mut c_void,
    name: *const c_char,
    description: *const c_char,
) -> usize {
    let opt = (
        props::dup(unsafe { cstr(name) }),
        props::dup(unsafe { cstr(description) }),
    );
    frame_rate(p, |o, _| {
        o.push(opt);
        o.len() - 1
    })
    .unwrap_or(DARRAY_INVALID)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_range_add(
    p: *mut c_void,
    min: media_frames_per_second,
    max: media_frames_per_second,
) -> usize {
    frame_rate(p, |_, r| {
        r.push((min, max));
        r.len() - 1
    })
    .unwrap_or(DARRAY_INVALID)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_option_insert(
    p: *mut c_void,
    idx: usize,
    name: *const c_char,
    description: *const c_char,
) {
    let opt = (
        props::dup(unsafe { cstr(name) }),
        props::dup(unsafe { cstr(description) }),
    );
    frame_rate(p, |o, _| props::frame_rate_insert(o, idx, opt));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_range_insert(
    p: *mut c_void,
    idx: usize,
    min: media_frames_per_second,
    max: media_frames_per_second,
) {
    frame_rate(p, |_, r| props::frame_rate_insert(r, idx, (min, max)));
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_options_count(p: *mut c_void) -> usize {
    frame_rate(p, |o, _| o.len()).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_option_name(
    p: *mut c_void,
    idx: usize,
) -> *const c_char {
    frame_rate(p, |o, _| {
        o.get(idx).map_or(ptr::null(), |(n, _)| str_ptr(n.as_ref()))
    })
    .unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_option_description(
    p: *mut c_void,
    idx: usize,
) -> *const c_char {
    frame_rate(p, |o, _| {
        o.get(idx).map_or(ptr::null(), |(_, d)| str_ptr(d.as_ref()))
    })
    .unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_ranges_count(p: *mut c_void) -> usize {
    frame_rate(p, |_, r| r.len()).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_range_min(
    p: *mut c_void,
    idx: usize,
) -> media_frames_per_second {
    frame_rate(p, |_, r| {
        r.get(idx).map(|(min, _)| *min).unwrap_or_default()
    })
    .unwrap_or_default()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_frame_rate_fps_range_max(
    p: *mut c_void,
    idx: usize,
) -> media_frames_per_second {
    frame_rate(p, |_, r| {
        r.get(idx).map(|(_, max)| *max).unwrap_or_default()
    })
    .unwrap_or_default()
}

/* ------------------------------------------------------------------------- */
/* groups */

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_group_type(p: *mut c_void) -> c_int {
    with_data(p, ty::GROUP, |d| match d {
        Data::Group { kind, .. } => *kind,
        _ => props::COMBO_INVALID,
    })
    .unwrap_or(props::COMBO_INVALID)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn obs_property_group_content(p: *mut c_void) -> *mut c_void {
    with_data(p, ty::GROUP, |d| match d {
        Data::Group { content, .. } => set_ptr(content),
        _ => ptr::null_mut(),
    })
    .unwrap_or(ptr::null_mut())
}
