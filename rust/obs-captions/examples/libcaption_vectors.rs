//! Deterministic wire fixtures consumed by the independent vendored C decoder.
use obs_captions::eia608;
use obs_captions::rollup::{Rollup, Rows};

fn main() {
    for fps in [30, 60] {
        for case in 0..5 {
            let rows = if case == 2 { Rows::Three } else { Rows::Two };
            let mut rollup = Rollup::new(fps, rows);
            let words: Vec<String> = match case {
                0 => ["T,", "T-", "T."].into_iter().map(String::from).collect(),
                1 => vec!["A".repeat(32), "B".repeat(32), String::from("END")],
                2 => vec![
                    "A".repeat(32),
                    "B".repeat(32),
                    "C".repeat(32),
                    String::from("END"),
                ],
                3 => vec![String::from("LLLL")],
                4 => vec![String::from("élÑñ"), String::from("A")],
                _ => unreachable!(),
            };
            for (index, word) in words.iter().enumerate() {
                rollup.word(index as f64, word).expect("valid fixture");
            }
            println!("CASE {case} {fps}");
            for &(frame, pair) in rollup.pairs() {
                println!("P {frame} {pair:04x}");
            }
            println!("END");
        }
    }
    for row in 0..15 {
        for col in 0..32 {
            for underline in [false, true] {
                let pair = eia608::row_column_preamble(row, col, underline);
                println!("PAC {row} {col} {} {pair:04x}", u8::from(underline));
            }
        }
    }
}
