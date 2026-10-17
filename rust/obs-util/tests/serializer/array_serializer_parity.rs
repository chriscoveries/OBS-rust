//! Tier 3: differential test of the Rust array-serializer shim against the C
//! oracle, and of the safe `ArrayOutput` core against the same oracle.
//!
//! Exclusion: writes always pass a non-null data pointer. NULL data with
//! size > 0 is undefined behavior in C (`memcpy`), so it is not compared.

use core::ffi::{c_int, c_void};
use core::ptr;

use obs_c_oracle::array_serializer::*;
use obs_c_oracle::darray::OracleDarray;
use obs_util::array_serializer::{ArrayOutput, seek_type_from_c};
use obs_util::ffi::array_serializer::{
    array_output_data, array_output_serializer_free, array_output_serializer_init,
    array_output_serializer_reset, serializer,
};
use obs_util::ffi::darray::darray;
use proptest::prelude::*;

#[derive(Debug, Clone)]
enum Op {
    Write(Vec<u8>),
    Seek(i64, c_int),
    GetPos,
    Reset,
}

fn op_strategy() -> impl Strategy<Value = Op> {
    prop_oneof![
        proptest::collection::vec(any::<u8>(), 0..40).prop_map(Op::Write),
        (-80i64..80, 0..=3).prop_map(|(o, k): (i64, c_int)| Op::Seek(o, k)),
        Just(Op::GetPos),
        Just(Op::Reset),
    ]
}

#[derive(Debug, PartialEq, Eq)]
struct Snapshot {
    ret: i64,
    cur_pos: usize,
    num: usize,
    capacity: usize,
    bytes: Vec<u8>,
}

fn bytes_of(array: *mut c_void, num: usize) -> Vec<u8> {
    if num == 0 {
        return Vec::new();
    }
    // SAFETY: a non-empty darray holds `num` initialized bytes.
    unsafe { core::slice::from_raw_parts(array as *const u8, num).to_vec() }
}

struct RustSide {
    s: Box<serializer>,
    d: Box<array_output_data>,
}

impl RustSide {
    fn new() -> Self {
        let mut s = Box::new(serializer {
            data: ptr::null_mut(),
            read: None,
            write: None,
            seek: None,
            get_pos: None,
        });
        let mut d = Box::new(array_output_data {
            bytes: darray {
                array: ptr::null_mut(),
                num: 0,
                capacity: 0,
            },
            cur_pos: 0,
        });
        // SAFETY: both boxes are valid, distinct heap allocations.
        unsafe { array_output_serializer_init(&raw mut *s, &raw mut *d) };
        Self { s, d }
    }

    fn run(&mut self, op: &Op) -> i64 {
        // SAFETY: init installed the callbacks; `s.data` is the boxed `d`;
        // write data pointers are non-null and valid for their length.
        unsafe {
            match op {
                Op::Write(v) => {
                    (self.s.write.unwrap())(self.s.data, v.as_ptr() as *const c_void, v.len())
                        as i64
                }
                Op::Seek(o, k) => (self.s.seek.unwrap())(self.s.data, *o, *k),
                Op::GetPos => (self.s.get_pos.unwrap())(self.s.data),
                Op::Reset => {
                    array_output_serializer_reset(&raw mut *self.d);
                    0
                }
            }
        }
    }

    fn snapshot(&self, ret: i64) -> Snapshot {
        Snapshot {
            ret,
            cur_pos: self.d.cur_pos,
            num: self.d.bytes.num,
            capacity: self.d.bytes.capacity,
            bytes: bytes_of(self.d.bytes.array, self.d.bytes.num),
        }
    }
}

impl Drop for RustSide {
    fn drop(&mut self) {
        // SAFETY: `d` was initialized by init and is freed exactly once.
        unsafe { array_output_serializer_free(&raw mut *self.d) };
    }
}

/// The safe core, driven with the same operations as the shim.
fn run_core(core: &mut ArrayOutput, op: &Op) -> i64 {
    match op {
        Op::Write(v) => core.write(v) as i64,
        Op::Seek(o, k) => core.seek(*o, seek_type_from_c(*k)),
        Op::GetPos => core.get_pos(),
        Op::Reset => {
            core.reset();
            0
        }
    }
}

fn core_snapshot(core: &ArrayOutput, ret: i64) -> Snapshot {
    Snapshot {
        ret,
        cur_pos: core.cur_pos(),
        num: core.bytes().len(),
        capacity: core.capacity(),
        bytes: core.bytes().to_vec(),
    }
}

struct OracleSide {
    s: Box<OracleSerializer>,
    d: Box<OracleArrayOutputData>,
}

impl OracleSide {
    fn new() -> Self {
        let mut s = Box::new(OracleSerializer {
            data: ptr::null_mut(),
            read: None,
            write: None,
            seek: None,
            get_pos: None,
        });
        let mut d = Box::new(OracleArrayOutputData {
            bytes: OracleDarray {
                array: ptr::null_mut(),
                num: 0,
                capacity: 0,
            },
            cur_pos: 0,
        });
        // SAFETY: both boxes are valid, distinct heap allocations.
        unsafe { oracle_array_output_serializer_init(&raw mut *s, &raw mut *d) };
        Self { s, d }
    }

    fn run(&mut self, op: &Op) -> i64 {
        // SAFETY: as for `RustSide::run`.
        unsafe {
            match op {
                Op::Write(v) => {
                    (self.s.write.unwrap())(self.s.data, v.as_ptr() as *const c_void, v.len())
                        as i64
                }
                Op::Seek(o, k) => (self.s.seek.unwrap())(self.s.data, *o, *k),
                Op::GetPos => (self.s.get_pos.unwrap())(self.s.data),
                Op::Reset => {
                    oracle_array_output_serializer_reset(&raw mut *self.d);
                    0
                }
            }
        }
    }

    fn snapshot(&self, ret: i64) -> Snapshot {
        Snapshot {
            ret,
            cur_pos: self.d.cur_pos,
            num: self.d.bytes.num,
            capacity: self.d.bytes.capacity,
            bytes: bytes_of(self.d.bytes.array, self.d.bytes.num),
        }
    }
}

impl Drop for OracleSide {
    fn drop(&mut self) {
        // SAFETY: `d` was initialized by init and is freed exactly once.
        unsafe { oracle_array_output_serializer_free(&raw mut *self.d) };
    }
}

proptest! {
    #[test]
    fn rust_matches_c(ops in proptest::collection::vec(op_strategy(), 0..60)) {
        let mut rust = RustSide::new();
        let mut core = ArrayOutput::new();
        let mut oracle = OracleSide::new();
        for op in &ops {
            let r = rust.run(op);
            let k = run_core(&mut core, op);
            let c = oracle.run(op);
            prop_assert_eq!(rust.snapshot(r), oracle.snapshot(c), "shim after {:?}", op);
            prop_assert_eq!(core_snapshot(&core, k), oracle.snapshot(c), "core after {:?}", op);
        }
    }
}
