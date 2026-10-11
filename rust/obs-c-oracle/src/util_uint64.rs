//! The `static inline` `util_mul_div64` in `util/util_uint64.h`, as a
//! non-inline wrapper (`oracle/util_uint64.c`).

unsafe extern "C" {
    pub fn oracle_util_mul_div64(num: u64, mul: u64, div: u64) -> u64;
}
