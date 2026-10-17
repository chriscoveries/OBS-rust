//! Tier 3: the Rust `darray` functions agree with the original C
//! `darray.h` (compiled into the oracle) on arbitrary operation sequences.

use core::ffi::c_void;
use core::ptr;
use core::slice;

use obs_c_oracle::darray::{self as c, OracleDarray};
use obs_util::darray::DArray;
use obs_util::ffi::darray::{self as r, darray};
use proptest::collection::vec;
use proptest::prelude::*;

#[derive(Debug, Clone)]
enum Op {
    PushBackArray(Vec<u8>),
    Reserve(usize),
    EnsureCapacity(usize),
    Resize(usize),
    Clear,
    Erase(usize),
    PopBack,
}

fn op() -> impl Strategy<Value = Op> {
    prop_oneof![
        vec(any::<u8>(), 0..=96).prop_map(Op::PushBackArray),
        (0usize..64).prop_map(Op::Reserve),
        (0usize..64).prop_map(Op::EnsureCapacity),
        (0usize..64).prop_map(Op::Resize),
        Just(Op::Clear),
        any::<usize>().prop_map(Op::Erase),
        Just(Op::PopBack),
    ]
}

fn bytes<'a>(array: *mut c_void, num: usize, es: usize) -> &'a [u8] {
    if num == 0 || array.is_null() {
        &[]
    } else {
        // SAFETY: a darray with `num > 0` owns at least `num * es` bytes.
        unsafe { slice::from_raw_parts(array as *const u8, num * es) }
    }
}

fn check(
    es: usize,
    a: &darray,
    b: &OracleDarray,
    safe: Option<&DArray<u8>>,
) -> Result<(), TestCaseError> {
    prop_assert_eq!(a.num, b.num);
    prop_assert_eq!(a.capacity, b.capacity);
    let ab = bytes(a.array, a.num, es);
    prop_assert_eq!(ab, bytes(b.array, b.num, es));
    if let Some(s) = safe {
        prop_assert_eq!(s.len(), a.num);
        prop_assert_eq!(s.capacity(), a.capacity);
        prop_assert_eq!(s.as_slice(), ab);
    }
    Ok(())
}

fn run(es: usize, ops: Vec<Op>) -> Result<(), TestCaseError> {
    let mut a = darray {
        array: ptr::null_mut(),
        num: 0,
        capacity: 0,
    };
    let mut b = OracleDarray {
        array: ptr::null_mut(),
        num: 0,
        capacity: 0,
    };
    let mut safe = DArray::<u8>::new();

    for op in ops {
        // SAFETY: `a` and `b` are valid darrays of `es`-byte items that
        // started empty and are only mutated through these functions; indices
        // are bounded by `num` and input slices are valid for their length.
        unsafe {
            match op {
                Op::PushBackArray(mut data) => {
                    data.truncate(data.len() / es * es);
                    let n = data.len() / es;
                    let p = data.as_ptr() as *const c_void;
                    let ra = r::darray_push_back_array(es, &mut a, p, n);
                    let rb = c::oracle_darray_push_back_array(es, &mut b, p, n);
                    prop_assert_eq!(ra, rb);
                    if es == 1 {
                        prop_assert_eq!(safe.push_back_array(&data), ra);
                    }
                }
                Op::Reserve(n) => {
                    r::darray_reserve(es, &mut a, n);
                    c::oracle_darray_reserve(es, &mut b, n);
                    safe.reserve(n);
                }
                Op::EnsureCapacity(n) => {
                    r::darray_ensure_capacity(es, &mut a, n);
                    c::oracle_darray_ensure_capacity(es, &mut b, n);
                    safe.ensure_capacity(n);
                }
                Op::Resize(n) => {
                    r::darray_resize(es, &mut a, n);
                    c::oracle_darray_resize(es, &mut b, n);
                    safe.resize(n);
                }
                Op::Clear => {
                    r::darray_clear(&mut a);
                    c::oracle_darray_clear(&mut b);
                    safe.clear();
                }
                Op::Erase(i) => {
                    if a.num > 0 {
                        let idx = i % a.num;
                        r::darray_erase(es, &mut a, idx);
                        c::oracle_darray_erase(es, &mut b, idx);
                        safe.erase(idx);
                    }
                }
                Op::PopBack => {
                    if a.num > 0 {
                        r::darray_pop_back(es, &mut a);
                        c::oracle_darray_pop_back(es, &mut b);
                        safe.pop_back();
                    }
                }
            }
        }
        check(es, &a, &b, (es == 1).then_some(&safe))?;
    }

    // SAFETY: both buffers came from the shared test bmalloc.
    unsafe {
        r::darray_free(&mut a);
        c::oracle_darray_free(&mut b);
    }
    Ok(())
}

proptest! {
    #[test]
    fn darray_ops_match_c(
        es in prop::sample::select(vec![1usize, 4, 12]),
        ops in vec(op(), 0..40),
    ) {
        run(es, ops)?;
    }
}
