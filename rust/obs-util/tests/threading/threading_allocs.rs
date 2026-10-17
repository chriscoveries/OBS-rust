//! `bnum_allocs` accounting for the threading handles: the C allocates
//! `struct os_event_data`/`struct os_sem_data` with `bzalloc`, so every
//! live handle is one allocation unit. In a `Box`-allocated port the
//! counter never moves.
//!
//! This lives in its own test binary with a single test on purpose:
//! `bnum_allocs` is a process-global counter, so asserting absolute
//! deltas races with any concurrent allocation — including a second test
//! in the same binary.
//!
//! Unix only, like the symbols it exercises.

#![cfg(unix)]

use std::ptr;

use obs_c_oracle as _; // links the oracle test stubs
use obs_util::ffi::bmem::bnum_allocs;
use obs_util::ffi::threading::{os_event_destroy, os_event_init, os_sem_destroy, os_sem_init};
use obs_util::threading::{Event, Sem};

#[test]
fn init_destroy_counts_one_bzalloc_per_live_handle() {
    let base = unsafe { bnum_allocs() };

    let mut event: *mut Event = ptr::null_mut();
    assert_eq!(unsafe { os_event_init(&mut event, 0) }, 0);
    assert_eq!(unsafe { bnum_allocs() }, base + 1);

    let mut sem: *mut Sem = ptr::null_mut();
    assert_eq!(unsafe { os_sem_init(&mut sem, 0) }, 0);
    assert_eq!(unsafe { bnum_allocs() }, base + 2);

    unsafe {
        os_event_destroy(event);
        os_sem_destroy(sem);
    }
    assert_eq!(unsafe { bnum_allocs() }, base);
}
