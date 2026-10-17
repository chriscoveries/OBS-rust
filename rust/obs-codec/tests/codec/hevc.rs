//! Tier 1: the safe HEVC helpers, mirroring `test/cmocka/test_hevc.c`.

use obs_c_oracle as _;
use obs_codec::hevc::{
    PRIORITY_DISPOSABLE, PRIORITY_HIGH, PRIORITY_HIGHEST, extract_headers, keyframe,
    packet_priority, to_hvcc,
};
use proptest as _;

/// A unit of `kind` (layer 0) with a 4-byte start code and one payload byte.
fn unit(kind: u8, payload: u8) -> Vec<u8> {
    vec![0, 0, 0, 1, kind << 1, 0x01, payload]
}

fn stream(units: &[(u8, u8)]) -> Vec<u8> {
    units.iter().flat_map(|&(k, p)| unit(k, p)).collect()
}

#[test]
fn keyframe_is_decided_by_the_first_unit_up_to_type_23() {
    assert!(keyframe(&stream(&[(32, 1), (19, 2)]))); // VPS, IDR_W_RADL
    assert!(keyframe(&stream(&[(21, 2)]))); // CRA
    assert!(keyframe(&stream(&[(23, 2)]))); // reserved IRAP
    assert!(!keyframe(&stream(&[(1, 2), (20, 3)]))); // TRAIL_R first
    assert!(!keyframe(&stream(&[(32, 1)])));
    assert!(!keyframe(&[]));
}

/// Reserved VCL types 24..=31 are skipped like parameter sets.
#[test]
fn reserved_vcl_types_are_skipped() {
    assert!(keyframe(&stream(&[(24, 1), (16, 2)])));
    assert!(!keyframe(&stream(&[(31, 1), (0, 2)])));
}

#[test]
fn priorities() {
    assert_eq!(packet_priority(&stream(&[(0, 1)]), 0), PRIORITY_DISPOSABLE);
    assert_eq!(
        packet_priority(&stream(&[(0, 1), (9, 2)]), 0),
        PRIORITY_HIGH
    );
    assert_eq!(
        packet_priority(&stream(&[(9, 1), (19, 2)]), 0),
        PRIORITY_HIGHEST
    );
    assert_eq!(packet_priority(&stream(&[(9, 1)]), 5), 5);
}

#[test]
fn to_hvcc_length_prefixes_units() {
    let out = to_hvcc(&stream(&[(1, 0xaa), (19, 0x88)]), false, 0);
    assert_eq!(
        out.payload,
        [0, 0, 0, 3, 0x02, 0x01, 0xaa, 0, 0, 0, 3, 0x26, 0x01, 0x88]
    );
    assert!(out.keyframe);
    assert_eq!(out.priority, PRIORITY_HIGHEST);
}

#[test]
fn extract_headers_splits_by_type() {
    let data = stream(&[(32, 1), (33, 2), (39, 3), (34, 4), (19, 5), (40, 6)]);
    let out = extract_headers(&data);
    let u = |i: usize| &data[i * 7..(i + 1) * 7];
    assert_eq!(out.header, [u(0), u(1), u(3)].concat());
    assert_eq!(out.sei, [u(2), u(5)].concat());
    assert_eq!(out.packet, u(4));
}
