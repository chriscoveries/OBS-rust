//! Tier 2: layout of `struct serializer` / `struct array_output_data` against
//! the C oracle, plus a shim smoke test mirroring `test_serializer.c`.

use core::ffi::c_void;
use core::mem::{align_of, offset_of, size_of};
use core::ptr;

use obs_c_oracle::array_serializer::*;
use obs_util::ffi::array_serializer::{
    array_output_data, array_output_serializer_free, array_output_serializer_init, serializer,
};
use obs_util::ffi::darray::darray;

#[test]
fn serializer_layout_matches_c() {
    // SAFETY: the oracle layout functions take no arguments and are pure.
    unsafe {
        assert_eq!(size_of::<serializer>(), oracle_serializer_size());
        assert_eq!(align_of::<serializer>(), oracle_serializer_align());
        assert_eq!(
            offset_of!(serializer, data),
            oracle_serializer_offset_data()
        );
        assert_eq!(
            offset_of!(serializer, read),
            oracle_serializer_offset_read()
        );
        assert_eq!(
            offset_of!(serializer, write),
            oracle_serializer_offset_write()
        );
        assert_eq!(
            offset_of!(serializer, seek),
            oracle_serializer_offset_seek()
        );
        assert_eq!(
            offset_of!(serializer, get_pos),
            oracle_serializer_offset_get_pos()
        );
    }
}

#[test]
fn array_output_data_layout_matches_c() {
    // SAFETY: the oracle layout functions take no arguments and are pure.
    unsafe {
        assert_eq!(
            size_of::<array_output_data>(),
            oracle_array_output_data_size()
        );
        assert_eq!(
            align_of::<array_output_data>(),
            oracle_array_output_data_align()
        );
        assert_eq!(
            offset_of!(array_output_data, bytes),
            oracle_array_output_data_offset_bytes()
        );
        assert_eq!(
            offset_of!(array_output_data, cur_pos),
            oracle_array_output_data_offset_cur_pos()
        );
    }
}

/// Mirrors `test/cmocka/test_serializer.c` through the function pointers.
#[test]
fn shim_serialize_smoke() {
    let mut s = serializer {
        data: ptr::null_mut(),
        read: None,
        write: None,
        seek: None,
        get_pos: None,
    };
    let mut d = array_output_data {
        bytes: darray {
            array: ptr::null_mut(),
            num: 0,
            capacity: 0,
        },
        cur_pos: 0,
    };
    // SAFETY: `s` and `d` are valid, distinct locals; the function pointers
    // are installed by init and receive `s.data`; the bytes read are within
    // the `num` bytes written.
    unsafe {
        array_output_serializer_init(&raw mut s, &raw mut d);
        assert!(s.read.is_none());
        for b in [0x01u8, 0xff, 0xe1] {
            assert_eq!(
                (s.write.unwrap())(s.data, &b as *const u8 as *const c_void, 1),
                1
            );
        }
        assert_eq!(d.bytes.num, 3);
        let got = core::slice::from_raw_parts(d.bytes.array as *const u8, d.bytes.num);
        assert_eq!(got, &[0x01, 0xff, 0xe1]);
        assert_eq!((s.get_pos.unwrap())(s.data), 3);
        array_output_serializer_free(&raw mut d);
    }
}
