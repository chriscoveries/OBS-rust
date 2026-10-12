//! CEA-608 roll-up (RU2/RU3) `cc_data` pair emitter.
//!
//! Mirrors the proven control sequence and frame pacing of the
//! `spikes/2026-10-12/rollup/rollup.c` harness, which FFmpeg's `subcc`
//! decoder recovered from real H.264 streams at 30 and 60 fps:
//!
//! - init: EDM, RU2/RU3, and a bottom-row PAC — each repeated twice
//! - words appear as they are spoken; a space runs between words on a row
//! - a word that would cross column 32 sends a carriage return first, then
//!   wraps; a word longer than one row is chunked with a CR between chunks
//! - 30 fps: one pair per picture; 60 fps: one pair on alternating pictures
//!   (CEA-708-B §4.4.2), both giving a 60 basic-character/s ceiling
//! - a one-character tail carries a null second byte, not a space
//!
//! The emitter rejects any character outside the basic North American set —
//! the same set `libcaption`'s `_eia608_from_utf8` maps for these codes — so
//! input should be filtered/transliterated upstream.

use crate::eia608::{self, SCREEN_COLS, SCREEN_ROWS};

/// A scheduled pair: the access-unit frame index and the `cc_data` pair to
/// attach to it (field 1).
pub type ScheduledPair = (u32, u16);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Rows {
    Two,
    Three,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RollupError {
    /// Input contained a character with no basic North American mapping.
    UnmappableChar(char),
    /// Non-monotonic timestamp.
    NonMonotonicTimestamp,
}

/// Roll-up pair scheduler. `fps` is the output frame rate; only 30 and 60
/// are supported, matching the proven harness.
pub struct Rollup {
    pairs: Vec<ScheduledPair>,
    /// Next frame index a pair may land on.
    slot: u32,
    /// Field-1 cadence: 1 at 30 fps, 2 (alternate pictures) at 60 fps.
    step: u32,
    fps: u32,
    col: usize,
    last_ts: f64,
}

impl Rollup {
    /// Starts a roll-up session: emits EDM, RU2/RU3 and the bottom-row PAC,
    /// each control repeated once (per CEA-608 practice), all due at frame 0.
    pub fn new(fps: u32, rows: Rows) -> Self {
        assert!(
            fps == 30 || fps == 60,
            "roll-up pacing supports 30 or 60 fps"
        );
        let mut r = Rollup {
            pairs: Vec::new(),
            slot: 0,
            step: if fps == 60 { 2 } else { 1 },
            fps,
            col: 0,
            last_ts: -1.0,
        };
        r.control(eia608::control::ERASE_DISPLAY_MEMORY, 0);
        r.control(
            match rows {
                Rows::Two => eia608::control::ROLL_UP_2,
                Rows::Three => eia608::control::ROLL_UP_3,
            },
            0,
        );
        let pac = eia608::row_column_preamble(SCREEN_ROWS - 1, 0, false);
        r.put(pac, 0);
        r.put(pac, 0);
        r
    }

    /// Queue a word to appear at `timestamp` seconds, like the proven
    /// `word()` routine: a space between words on a row, a CR when the word
    /// would cross column 32, and chunked hard-wrap for over-long words.
    pub fn word(&mut self, timestamp: f64, word: &str) -> Result<(), RollupError> {
        if !timestamp.is_finite() || timestamp < 0.0 || timestamp < self.last_ts {
            return Err(RollupError::NonMonotonicTimestamp);
        }
        self.last_ts = timestamp;
        let due = (timestamp * self.fps as f64 - 1e-9).ceil() as u32;
        let chars: Vec<char> = word.chars().collect();
        for &c in &chars {
            if eia608::basicna_code(c).is_none() {
                return Err(RollupError::UnmappableChar(c));
            }
        }

        let mut n = chars.len();
        let mut s = &chars[..];
        if self.col != 0 && self.col + 1 + n > SCREEN_COLS {
            self.control(eia608::control::CARRIAGE_RETURN, due);
            self.col = 0;
        }
        if self.col != 0 {
            self.text(" ", due);
        }
        while n > 0 {
            let chunk = n.min(SCREEN_COLS - self.col);
            let end = chunk;
            for pair in s[..end].chunks(2) {
                let code = if pair.len() == 2 {
                    eia608::from_basicna(pair[0], pair[1]).unwrap()
                } else {
                    eia608::from_utf8_1(pair[0]).unwrap()
                };
                self.put(code, due);
            }
            self.col += end;
            s = &s[end..];
            n -= end;
            if n > 0 {
                self.control(eia608::control::CARRIAGE_RETURN, due);
                self.col = 0;
            }
        }
        Ok(())
    }

    /// The scheduled `(frame, cc_data)` pairs in emission order.
    pub fn pairs(&self) -> &[ScheduledPair] {
        &self.pairs
    }

    fn put(&mut self, data: u16, due: u32) {
        if self.slot < due {
            self.slot = due;
        }
        self.slot += (self.step - self.slot % self.step) % self.step;
        self.pairs.push((self.slot, data));
        self.slot += self.step;
    }

    fn control(&mut self, cmd: u16, due: u32) {
        let code = eia608::control_command(cmd);
        // Control commands are repeated, not interpreted as extra characters.
        self.put(code, due);
        self.put(code, due);
    }

    fn text(&mut self, s: &str, due: u32) {
        let chars: Vec<char> = s.chars().collect();
        for pair in chars.chunks(2) {
            let code = if pair.len() == 2 {
                eia608::from_basicna(pair[0], pair[1]).unwrap()
            } else {
                eia608::from_utf8_1(pair[0]).unwrap()
            };
            self.put(code, due);
        }
        self.col += chars.len();
    }
}
