//! `oracle_*` builds of `libobs/obs-properties.c` (oracle/obs_properties.c).
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_int, c_longlong, c_void};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct media_frames_per_second {
    pub numerator: u32,
    pub denominator: u32,
}

pub type obs_property_modified_t =
    Option<unsafe extern "C" fn(props: *mut c_void, p: *mut c_void, settings: *mut c_void) -> bool>;
pub type obs_property_modified2_t = Option<
    unsafe extern "C" fn(
        priv_: *mut c_void,
        props: *mut c_void,
        p: *mut c_void,
        settings: *mut c_void,
    ) -> bool,
>;
pub type obs_property_clicked_t =
    Option<unsafe extern "C" fn(props: *mut c_void, p: *mut c_void, data: *mut c_void) -> bool>;
pub type param_destroy_t = Option<unsafe extern "C" fn(param: *mut c_void)>;

unsafe extern "C" {
    pub fn oracle_obs_properties_create() -> *mut c_void;
    pub fn oracle_obs_properties_create_param(
        param: *mut c_void,
        destroy: param_destroy_t,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_destroy(props: *mut c_void);
    pub fn oracle_obs_properties_set_flags(props: *mut c_void, flags: u32);
    pub fn oracle_obs_properties_get_flags(props: *mut c_void) -> u32;
    pub fn oracle_obs_properties_set_param(
        props: *mut c_void,
        param: *mut c_void,
        destroy: param_destroy_t,
    );
    pub fn oracle_obs_properties_get_param(props: *mut c_void) -> *mut c_void;
    pub fn oracle_obs_properties_first(props: *mut c_void) -> *mut c_void;
    pub fn oracle_obs_properties_get(props: *mut c_void, property: *const c_char) -> *mut c_void;
    pub fn oracle_obs_properties_get_parent(props: *mut c_void) -> *mut c_void;
    pub fn oracle_obs_properties_remove_by_name(props: *mut c_void, property: *const c_char);
    pub fn oracle_obs_properties_apply_settings(props: *mut c_void, settings: *mut c_void);
    pub fn oracle_obs_properties_add_bool(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_int(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        min: c_int,
        max: c_int,
        step: c_int,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_float(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        min: f64,
        max: f64,
        step: f64,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_int_slider(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        min: c_int,
        max: c_int,
        step: c_int,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_float_slider(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        min: f64,
        max: f64,
        step: f64,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_text(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        type_: c_int,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_path(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        type_: c_int,
        filter: *const c_char,
        default_path: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_list(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        type_: c_int,
        format: c_int,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_color(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_color_alpha(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_button(
        props: *mut c_void,
        name: *const c_char,
        text: *const c_char,
        callback: obs_property_clicked_t,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_button2(
        props: *mut c_void,
        name: *const c_char,
        text: *const c_char,
        callback: obs_property_clicked_t,
        priv_: *mut c_void,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_font(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_editable_list(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        type_: c_int,
        filter: *const c_char,
        default_path: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_frame_rate(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> *mut c_void;
    pub fn oracle_obs_properties_add_group(
        props: *mut c_void,
        name: *const c_char,
        description: *const c_char,
        type_: c_int,
        group: *mut c_void,
    ) -> *mut c_void;
    pub fn oracle_obs_property_set_modified_callback(
        p: *mut c_void,
        modified: obs_property_modified_t,
    );
    pub fn oracle_obs_property_set_modified_callback2(
        p: *mut c_void,
        modified: obs_property_modified2_t,
        priv_: *mut c_void,
    );
    pub fn oracle_obs_property_modified(p: *mut c_void, settings: *mut c_void) -> bool;
    pub fn oracle_obs_property_button_clicked(p: *mut c_void, obj: *mut c_void) -> bool;
    pub fn oracle_obs_property_set_visible(p: *mut c_void, visible: bool);
    pub fn oracle_obs_property_set_enabled(p: *mut c_void, enabled: bool);
    pub fn oracle_obs_property_set_description(p: *mut c_void, description: *const c_char);
    pub fn oracle_obs_property_set_long_description(
        p: *mut c_void,
        long_description: *const c_char,
    );
    pub fn oracle_obs_property_name(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_description(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_long_description(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_get_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_enabled(p: *mut c_void) -> bool;
    pub fn oracle_obs_property_visible(p: *mut c_void) -> bool;
    pub fn oracle_obs_property_next(p: *mut *mut c_void) -> bool;
    pub fn oracle_obs_property_int_min(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_int_max(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_int_step(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_int_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_int_suffix(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_float_min(p: *mut c_void) -> f64;
    pub fn oracle_obs_property_float_max(p: *mut c_void) -> f64;
    pub fn oracle_obs_property_float_step(p: *mut c_void) -> f64;
    pub fn oracle_obs_property_float_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_float_suffix(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_text_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_text_monospace(p: *mut c_void) -> bool;
    pub fn oracle_obs_property_text_info_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_text_info_word_wrap(p: *mut c_void) -> bool;
    pub fn oracle_obs_property_path_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_path_filter(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_path_default_path(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_list_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_list_format(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_int_set_limits(p: *mut c_void, min: c_int, max: c_int, step: c_int);
    pub fn oracle_obs_property_float_set_limits(p: *mut c_void, min: f64, max: f64, step: f64);
    pub fn oracle_obs_property_int_set_suffix(p: *mut c_void, suffix: *const c_char);
    pub fn oracle_obs_property_float_set_suffix(p: *mut c_void, suffix: *const c_char);
    pub fn oracle_obs_property_text_set_monospace(p: *mut c_void, monospace: bool);
    pub fn oracle_obs_property_text_set_info_type(p: *mut c_void, type_: c_int);
    pub fn oracle_obs_property_text_set_info_word_wrap(p: *mut c_void, word_wrap: bool);
    pub fn oracle_obs_property_button_set_type(p: *mut c_void, type_: c_int);
    pub fn oracle_obs_property_button_set_url(p: *mut c_void, url: *mut c_char);
    pub fn oracle_obs_property_list_clear(p: *mut c_void);
    pub fn oracle_obs_property_list_add_string(
        p: *mut c_void,
        name: *const c_char,
        val: *const c_char,
    ) -> usize;
    pub fn oracle_obs_property_list_add_int(
        p: *mut c_void,
        name: *const c_char,
        val: c_longlong,
    ) -> usize;
    pub fn oracle_obs_property_list_add_float(
        p: *mut c_void,
        name: *const c_char,
        val: f64,
    ) -> usize;
    pub fn oracle_obs_property_list_add_bool(
        p: *mut c_void,
        name: *const c_char,
        val: bool,
    ) -> usize;
    pub fn oracle_obs_property_list_insert_string(
        p: *mut c_void,
        idx: usize,
        name: *const c_char,
        val: *const c_char,
    );
    pub fn oracle_obs_property_list_insert_int(
        p: *mut c_void,
        idx: usize,
        name: *const c_char,
        val: c_longlong,
    );
    pub fn oracle_obs_property_list_insert_float(
        p: *mut c_void,
        idx: usize,
        name: *const c_char,
        val: f64,
    );
    pub fn oracle_obs_property_list_insert_bool(
        p: *mut c_void,
        idx: usize,
        name: *const c_char,
        val: bool,
    );
    pub fn oracle_obs_property_list_item_disable(p: *mut c_void, idx: usize, disabled: bool);
    pub fn oracle_obs_property_list_item_disabled(p: *mut c_void, idx: usize) -> bool;
    pub fn oracle_obs_property_list_item_remove(p: *mut c_void, idx: usize);
    pub fn oracle_obs_property_list_item_count(p: *mut c_void) -> usize;
    pub fn oracle_obs_property_list_item_name(p: *mut c_void, idx: usize) -> *const c_char;
    pub fn oracle_obs_property_list_item_string(p: *mut c_void, idx: usize) -> *const c_char;
    pub fn oracle_obs_property_list_item_int(p: *mut c_void, idx: usize) -> c_longlong;
    pub fn oracle_obs_property_list_item_float(p: *mut c_void, idx: usize) -> f64;
    pub fn oracle_obs_property_list_item_bool(p: *mut c_void, idx: usize) -> bool;
    pub fn oracle_obs_property_editable_list_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_editable_list_filter(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_editable_list_default_path(p: *mut c_void) -> *const c_char;
    pub fn oracle_obs_property_frame_rate_clear(p: *mut c_void);
    pub fn oracle_obs_property_frame_rate_options_clear(p: *mut c_void);
    pub fn oracle_obs_property_frame_rate_fps_ranges_clear(p: *mut c_void);
    pub fn oracle_obs_property_frame_rate_option_add(
        p: *mut c_void,
        name: *const c_char,
        description: *const c_char,
    ) -> usize;
    pub fn oracle_obs_property_frame_rate_fps_range_add(
        p: *mut c_void,
        min: media_frames_per_second,
        max: media_frames_per_second,
    ) -> usize;
    pub fn oracle_obs_property_frame_rate_option_insert(
        p: *mut c_void,
        idx: usize,
        name: *const c_char,
        description: *const c_char,
    );
    pub fn oracle_obs_property_frame_rate_fps_range_insert(
        p: *mut c_void,
        idx: usize,
        min: media_frames_per_second,
        max: media_frames_per_second,
    );
    pub fn oracle_obs_property_frame_rate_options_count(p: *mut c_void) -> usize;
    pub fn oracle_obs_property_frame_rate_option_name(p: *mut c_void, idx: usize) -> *const c_char;
    pub fn oracle_obs_property_frame_rate_option_description(
        p: *mut c_void,
        idx: usize,
    ) -> *const c_char;
    pub fn oracle_obs_property_frame_rate_fps_ranges_count(p: *mut c_void) -> usize;
    pub fn oracle_obs_property_frame_rate_fps_range_min(
        p: *mut c_void,
        idx: usize,
    ) -> media_frames_per_second;
    pub fn oracle_obs_property_frame_rate_fps_range_max(
        p: *mut c_void,
        idx: usize,
    ) -> media_frames_per_second;
    pub fn oracle_obs_property_group_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_group_content(p: *mut c_void) -> *mut c_void;
    pub fn oracle_obs_property_button_type(p: *mut c_void) -> c_int;
    pub fn oracle_obs_property_button_url(p: *mut c_void) -> *const c_char;
}
