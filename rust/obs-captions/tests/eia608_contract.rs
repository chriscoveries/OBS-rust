//! Independent byte-level contracts for the basic channel-1 subset.
use obs_captions::eia608;

#[test]
fn control_classification_exhaustive() {
    for pair in 0..=u16::MAX {
        let high = (pair >> 8) & 0x7f;
        let low = pair & 0x7f;
        let expected = if high == 0x14 && (0x20..=0x2f).contains(&low) {
            Some((high << 8) | low)
        } else {
            None
        };
        assert_eq!(eia608::parse_control(pair), expected, "pair {pair:04x}");
    }
}

#[test]
fn every_basic_glyph_pair_is_text() {
    for high in 0x20..=0x7f {
        for low in 0x20..=0x7f {
            let pair = eia608::parity((high << 8) | low);
            assert!(eia608::parse_control(pair).is_none(), "pair {pair:04x}");
            let (a, b) = eia608::basicna_decode(pair);
            assert!(a.is_some() && b.is_some());
        }
    }
}

#[test]
fn preambles_have_correct_parity_and_position() {
    // Decoder row order, including the reserved slot, from the wire layout.
    // This is deliberately not the encoder's inverse table.
    const ROWS: [i32; 16] = [10, -1, 0, 1, 2, 3, 11, 12, 13, 14, 4, 5, 6, 7, 8, 9];
    for row in 0..15 {
        for col in 0..32 {
            for underline in [false, true] {
                let pair = eia608::row_column_preamble(row, col, underline);
                let high = (pair >> 8) as u8;
                let low = pair as u8;
                assert_eq!(high.count_ones() % 2, 1);
                assert_eq!(low.count_ones() % 2, 1, "row {row} col {col}");
                let raw = pair & 0x7f7f;
                let index = usize::from(((raw & 0x0700) >> 7) | ((raw & 0x0020) >> 5));
                assert_eq!(ROWS[index], row as i32);
                assert_ne!(raw & 0x10, 0, "indent selector must be set");
                assert_eq!(usize::from((raw & 0x0e) >> 1) * 4, col / 4 * 4);
                assert_eq!(raw & 1, u16::from(underline));
                assert_eq!(raw & 0x0800, 0, "channel 1");
            }
        }
    }
}

#[test]
fn bottom_row_indent_zero_has_independent_golden_bytes() {
    // 0x14: row-code high bits; 0x70: bottom row + indent selector, offset 0.
    assert_eq!(eia608::row_column_preamble(14, 0, false), 0x9470);
    assert_eq!(eia608::row_column_preamble(14, 4, true), 0x9473);
}

#[test]
fn parity_roundtrips_all_payloads() {
    for raw in 0..=u16::MAX {
        let pair = eia608::parity(raw);
        assert_eq!((pair >> 8).count_ones() % 2, 1);
        assert_eq!((pair & 0xff).count_ones() % 2, 1);
        assert_eq!(eia608::parity_strip(pair), raw & 0x7f7f);
    }
}
