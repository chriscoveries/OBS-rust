//! Direct differential coverage of obs-nal start-code search (#144).
//! Arbitrary bytes are searched at four address alignments, including empty
//! and short inputs. NULL, near-zero addresses and reversed ranges are not
//! generated: positive ranges must be inside a live allocation. Eight bytes
//! of prefix padding keep the C search's end-minus-six arithmetic inside it.

#![no_main]

use libfuzzer_sys::fuzz_target;
use obs_c_oracle::nal::oracle_obs_nal_find_startcode;
use obs_codec::{ffi::nal::obs_nal_find_startcode, nal::find_startcode};
use obs_util as _; // links the allocator used by the C oracle

fuzz_target!(|input: &[u8]| {
    for alignment in 0..4 {
        let offset = 8 + alignment;
        let mut backing = vec![0xee; offset];
        backing.extend_from_slice(input);
        backing.extend_from_slice(&[0xee; 8]);
        let data = &backing[offset..offset + input.len()];
        let start = data.as_ptr();
        // SAFETY: start and end delimit a readable range in backing; prefix
        // padding also permits the original C search's backward arithmetic.
        unsafe {
            let end = start.add(data.len());
            let expected = oracle_obs_nal_find_startcode(start, end);
            assert_eq!(obs_nal_find_startcode(start, end), expected);
            assert_eq!(start.add(find_startcode(data)), expected);
        }
    }
});
