//! Tier 1: safe-core tests. `bitstream_test` mirrors
//! `test/cmocka/test_bitstream.c` assertion for assertion.

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)
use obs_util::bitstream::BitstreamReader;

#[test]
fn bitstream_test() {
    let data: [u8; 6] = [0x34, 0xff, 0xe1, 0x23, 0x91, 0x45];

    // set len to one less than the array to show that we stop reading at that len
    let mut reader = BitstreamReader::new(&data[..5]);

    assert_eq!(reader.read_bits(8), 0x34);
    assert_eq!(reader.read_bits(1), 1);
    assert_eq!(reader.read_bits(3), 7);
    assert_eq!(reader.read_bits(4), 0xF);
    assert_eq!(reader.r8(), 0xe1);
    assert_eq!(reader.r16(), 0x2391);

    // test reached end
    assert_eq!(reader.r8(), 0);
}

#[test]
fn read_bits_over_8_keeps_low_byte() {
    let data = [0xAB, 0xCD];
    let mut reader = BitstreamReader::new(&data);
    assert_eq!(reader.read_bits(16), 0xCD);

    let mut reader = BitstreamReader::new(&data);
    // 9 bits: 1010_1011_1 -> low 8 bits 0101_0111
    assert_eq!(reader.read_bits(9), 0x57);
}

#[test]
fn read_zero_bits_consumes_nothing() {
    let data = [0x80];
    let mut reader = BitstreamReader::new(&data);
    assert_eq!(reader.read_bits(0), 0);
    assert_eq!(reader.read_bit(), 1);
}

#[test]
fn empty_buffer_reads_zero() {
    let mut reader = BitstreamReader::new(&[]);
    assert_eq!(reader.read_bit(), 0);
    assert_eq!(reader.r16(), 0);
}

#[test]
fn safe_core_does_not_wrap_past_byte_255() {
    let mut data = [0x11u8; 300];
    data[0] = 0xAA;
    data[256] = 0x55;
    let mut reader = BitstreamReader::new(&data);
    for _ in 0..256 {
        reader.r8();
    }
    assert_eq!(reader.r8(), 0x55);
}

#[test]
fn r16_is_big_endian_and_unaligned_safe() {
    let data = [0xAB, 0xCD, 0xEF];
    let mut reader = BitstreamReader::new(&data);
    assert_eq!(reader.r16(), 0xABCD);

    // Starting 4 bits in, r16 spans three bytes: 0xBCDE.
    let mut reader = BitstreamReader::new(&data);
    assert_eq!(reader.read_bits(4), 0xA);
    assert_eq!(reader.r16(), 0xBCDE);
}

#[test]
fn reads_past_end_keep_returning_zero() {
    let data = [0xFF];
    let mut reader = BitstreamReader::new(&data);
    assert_eq!(reader.r8(), 0xFF);
    for _ in 0..4 {
        assert_eq!(reader.r16(), 0);
        assert_eq!(reader.read_bit(), 0);
    }
}
