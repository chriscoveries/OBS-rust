//! Tier 3: `util/threading-windows.c` against the Rust shims.
//!
//! `os_event_t` and `os_sem_t` are opaque, so there is no layout test.
//! Single-threaded op sequences only issue `wait` / `sem_wait` when a
//! shadow model says they return immediately. `timedwait` uses 0 ms.
//!
//! Excluded: a NULL out-pointer with a value `CreateEvent` /
//! `CreateSemaphore` would accept. The C writes the handle through that
//! pointer. The shim closes the object and returns `EINVAL`.

#![cfg(windows)]

use core::ffi::{c_char, c_int, c_ulong, c_void};
use std::ffi::CString;
use std::ptr;

use obs_c_oracle::threading::{
    OracleEvent, OracleSem, oracle_os_event_destroy, oracle_os_event_init, oracle_os_event_reset,
    oracle_os_event_signal, oracle_os_event_timedwait, oracle_os_event_try, oracle_os_event_wait,
    oracle_os_sem_destroy, oracle_os_sem_init, oracle_os_sem_post, oracle_os_sem_trywait,
    oracle_os_sem_wait, oracle_os_set_thread_name,
};
use obs_util::ffi::threading::{
    os_event_destroy, os_event_init, os_event_reset, os_event_signal, os_event_timedwait,
    os_event_try, os_event_wait, os_sem_destroy, os_sem_init, os_sem_post, os_sem_wait,
    os_set_thread_name,
};
use obs_util::threading::Sem;
use proptest::prelude::*;

const MANUAL: c_int = 1;
const SEM_MAX: c_int = 0x7fff_ffff;

#[derive(Debug, Clone)]
enum Op {
    Signal,
    Try,
    Reset,
    TimedWait,
    WaitIfSignalled,
    Post,
    WaitIfPositive,
    SemTry,
}

struct EventApi {
    init: unsafe fn(*mut *mut u8, c_int) -> c_int,
    destroy: unsafe fn(*mut u8),
    wait: unsafe fn(*mut u8) -> c_int,
    timedwait: unsafe fn(*mut u8, c_ulong) -> c_int,
    try_: unsafe fn(*mut u8) -> c_int,
    signal: unsafe fn(*mut u8) -> c_int,
    reset: unsafe fn(*mut u8),
}

struct SemApi {
    init: unsafe fn(*mut *mut u8, c_int) -> c_int,
    destroy: unsafe fn(*mut u8),
    post: unsafe fn(*mut u8) -> c_int,
    wait: unsafe fn(*mut u8) -> c_int,
    try_: unsafe fn(*mut u8) -> c_int,
}

fn rust_event() -> EventApi {
    EventApi {
        init: |e, t| unsafe { os_event_init(e.cast(), t) },
        destroy: |e| unsafe { os_event_destroy(e.cast()) },
        wait: |e| unsafe { os_event_wait(e.cast()) },
        timedwait: |e, ms| unsafe { os_event_timedwait(e.cast(), ms) },
        try_: |e| unsafe { os_event_try(e.cast()) },
        signal: |e| unsafe { os_event_signal(e.cast()) },
        reset: |e| unsafe { os_event_reset(e.cast()) },
    }
}

fn oracle_event() -> EventApi {
    EventApi {
        init: |e, t| unsafe { oracle_os_event_init(e.cast::<*mut OracleEvent>(), t) },
        destroy: |e| unsafe { oracle_os_event_destroy(e.cast()) },
        wait: |e| unsafe { oracle_os_event_wait(e.cast()) },
        timedwait: |e, ms| unsafe { oracle_os_event_timedwait(e.cast(), ms) },
        try_: |e| unsafe { oracle_os_event_try(e.cast()) },
        signal: |e| unsafe { oracle_os_event_signal(e.cast()) },
        reset: |e| unsafe { oracle_os_event_reset(e.cast()) },
    }
}

fn rust_sem() -> SemApi {
    SemApi {
        init: |s, v| unsafe { os_sem_init(s.cast(), v) },
        destroy: |s| unsafe { os_sem_destroy(s.cast()) },
        post: |s| unsafe { os_sem_post(s.cast()) },
        wait: |s| unsafe { os_sem_wait(s.cast()) },
        try_: |s| {
            if unsafe { &*s.cast::<Sem>() }.try_wait() {
                0
            } else {
                -1
            }
        },
    }
}

fn oracle_sem() -> SemApi {
    SemApi {
        init: |s, v| unsafe { oracle_os_sem_init(s.cast::<*mut OracleSem>(), v) },
        destroy: |s| unsafe { oracle_os_sem_destroy(s.cast()) },
        post: |s| unsafe { oracle_os_sem_post(s.cast()) },
        wait: |s| unsafe { oracle_os_sem_wait(s.cast()) },
        try_: |s| unsafe { oracle_os_sem_trywait(s.cast()) },
    }
}

fn run(manual: bool, initial: c_int, ops: &[Op], event: &EventApi, sem: &SemApi) -> Vec<c_int> {
    let mut results = Vec::new();
    let mut ev: *mut u8 = ptr::null_mut();
    let mut sm: *mut u8 = ptr::null_mut();
    let mut signalled = false;
    let mut count: i64 = i64::from(initial);

    results.push(unsafe { (event.init)(&mut ev, if manual { MANUAL } else { 0 }) });
    results.push(unsafe { (sem.init)(&mut sm, initial) });
    assert!(!ev.is_null() && !sm.is_null());

    for op in ops {
        let code = match op {
            Op::Signal => {
                signalled = true;
                unsafe { (event.signal)(ev) }
            }
            Op::Try => {
                let c = unsafe { (event.try_)(ev) };
                if c == 0 {
                    signalled = manual;
                }
                c
            }
            Op::Reset => {
                signalled = false;
                unsafe { (event.reset)(ev) };
                continue;
            }
            Op::TimedWait => {
                let c = unsafe { (event.timedwait)(ev, 0) };
                if c == 0 {
                    signalled = manual;
                }
                c
            }
            Op::WaitIfSignalled => {
                if !signalled {
                    continue;
                }
                let c = unsafe { (event.wait)(ev) };
                signalled = manual;
                c
            }
            Op::Post => {
                if count >= i64::from(SEM_MAX) {
                    continue;
                }
                count += 1;
                unsafe { (sem.post)(sm) }
            }
            Op::WaitIfPositive => {
                if count <= 0 {
                    continue;
                }
                let c = unsafe { (sem.wait)(sm) };
                count -= 1;
                c
            }
            Op::SemTry => {
                let c = unsafe { (sem.try_)(sm) };
                if c == 0 {
                    count -= 1;
                }
                c
            }
        };
        results.push(code);
    }

    unsafe {
        (event.destroy)(ev);
        (sem.destroy)(sm);
    }
    results
}

#[test]
fn null_args_match() {
    unsafe {
        assert_eq!(
            os_event_wait(ptr::null_mut()),
            oracle_os_event_wait(ptr::null_mut())
        );
        assert_eq!(
            os_event_timedwait(ptr::null_mut(), 1),
            oracle_os_event_timedwait(ptr::null_mut(), 1)
        );
        assert_eq!(
            os_event_try(ptr::null_mut()),
            oracle_os_event_try(ptr::null_mut())
        );
        assert_eq!(
            os_event_signal(ptr::null_mut()),
            oracle_os_event_signal(ptr::null_mut())
        );
        os_event_reset(ptr::null_mut());
        oracle_os_event_reset(ptr::null_mut());
        os_event_destroy(ptr::null_mut());
        oracle_os_event_destroy(ptr::null_mut());

        assert_eq!(os_sem_post(ptr::null_mut()), -1);
        assert_eq!(oracle_os_sem_post(ptr::null_mut()), -1);
        assert_eq!(os_sem_wait(ptr::null_mut()), -1);
        assert_eq!(oracle_os_sem_wait(ptr::null_mut()), -1);
        os_sem_destroy(ptr::null_mut());
        oracle_os_sem_destroy(ptr::null_mut());
    }
}

#[test]
fn sem_init_negative_matches() {
    for value in [-1, c_int::MIN] {
        let mut rust_sem: *mut Sem = ptr::null_mut();
        let mut oracle_sem: *mut OracleSem = ptr::null_mut();
        let rust = unsafe { os_sem_init(&mut rust_sem, value) };
        let oracle = unsafe { oracle_os_sem_init(&mut oracle_sem, value) };
        assert_eq!(rust, oracle, "sem_init({value})");
        assert_eq!(rust, -1);
        assert!(rust_sem.is_null() && oracle_sem.is_null());
    }
}

#[test]
fn sem_post_at_max_matches() {
    let mut rust_sem: *mut Sem = ptr::null_mut();
    let mut oracle_sem: *mut OracleSem = ptr::null_mut();
    assert_eq!(unsafe { os_sem_init(&mut rust_sem, SEM_MAX) }, 0);
    assert_eq!(unsafe { oracle_os_sem_init(&mut oracle_sem, SEM_MAX) }, 0);
    assert_eq!(unsafe { os_sem_post(rust_sem) }, unsafe {
        oracle_os_sem_post(oracle_sem)
    });
    assert_eq!(unsafe { os_sem_post(rust_sem) }, -1);
    unsafe {
        os_sem_destroy(rust_sem);
        oracle_os_sem_destroy(oracle_sem);
    }
}

#[test]
fn thread_name_matches() {
    let hello = CString::new("héllo").unwrap();
    let long = CString::new("a-very-long-thread-name").unwrap();
    let invalid = CString::new(vec![0xff, b'q']).unwrap();
    let samples = [
        c"obs-test".as_ptr(),
        c"".as_ptr(),
        ptr::null(),
        hello.as_ptr(),
        long.as_ptr(),
        invalid.as_ptr(),
    ];
    for name in samples {
        let rust = thread_description_after(os_set_thread_name, name);
        let oracle = thread_description_after(oracle_os_set_thread_name, name);
        assert_eq!(rust, oracle, "name {name:?}");
    }
}

fn thread_description_after(
    set: unsafe extern "C" fn(*const c_char),
    name: *const c_char,
) -> Result<Vec<u16>, i32> {
    let name = name as usize;
    std::thread::spawn(move || {
        let name = name as *const c_char;
        unsafe { set(name) };
        current_thread_description()
    })
    .join()
    .unwrap()
}

fn current_thread_description() -> Result<Vec<u16>, i32> {
    type GetThreadDescription = unsafe extern "system" fn(*mut c_void, *mut *mut u16) -> i32;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn LoadLibraryW(name: *const u16) -> *mut c_void;
        fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *const c_void;
        fn FreeLibrary(module: *mut c_void) -> i32;
        fn GetCurrentThread() -> *mut c_void;
        fn LocalFree(mem: *mut c_void) -> *mut c_void;
    }
    unsafe {
        let kernelbase: Vec<u16> = "KernelBase.dll"
            .encode_utf16()
            .chain(core::iter::once(0))
            .collect();
        let module = LoadLibraryW(kernelbase.as_ptr());
        if module.is_null() {
            return Err(-1);
        }
        let sym = GetProcAddress(module, c"GetThreadDescription".as_ptr());
        if sym.is_null() {
            FreeLibrary(module);
            return Err(-1);
        }
        let get = core::mem::transmute::<*const c_void, GetThreadDescription>(sym);
        let mut wide: *mut u16 = ptr::null_mut();
        let hr = get(GetCurrentThread(), &mut wide);
        let result = if hr < 0 || wide.is_null() {
            Err(hr)
        } else {
            let mut out = Vec::new();
            let mut p = wide;
            while *p != 0 {
                out.push(*p);
                p = p.add(1);
            }
            LocalFree(wide.cast());
            Ok(out)
        };
        FreeLibrary(module);
        result
    }
}

proptest! {
    #[test]
    fn event_and_sem_ops_match_c(
        manual in any::<bool>(),
        initial in 0..8i32,
        ops in proptest::collection::vec(
            prop_oneof![
                Just(Op::Signal),
                Just(Op::Try),
                Just(Op::Reset),
                Just(Op::TimedWait),
                Just(Op::WaitIfSignalled),
                Just(Op::Post),
                Just(Op::WaitIfPositive),
                Just(Op::SemTry),
            ],
            0..32
        ),
    ) {
        let rust = run(manual, initial, &ops, &rust_event(), &rust_sem());
        let oracle = run(manual, initial, &ops, &oracle_event(), &oracle_sem());
        prop_assert_eq!(rust, oracle);
    }
}
