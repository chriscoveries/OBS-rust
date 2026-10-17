//! Tier 1: 1:1 Rust port of the event, semaphore and thread-name cases in
//! `test/cmocka/test_threading.c`, plus edge cases.
//!
//! Deterministic: no sleeps; synchronization only through the events and
//! semaphores under test and through thread joins.
//!
//! Not ported here, by design:
//! - The `test_atomic_*` cases exercise the `static inline` helpers in
//!   `libobs/util/threading-posix.h`, header code outside this unit. They
//!   keep running in C in both OFF and ON builds (Tier 2).
//! - `test_sem_null` and NULL event handles: the safe API has no nullable
//!   handles. NULL behavior lives in the C shim and is covered by the
//!   unchanged C test (Tier 2) and the parity test (Tier 3).
//! - `test_set_thread_name` calls the C entry point; the safe core only
//!   owns the glibc truncation rule, tested below.

use std::sync::Arc;

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)
#[cfg(unix)]
use obs_util::threading::Sem;
use obs_util::threading::{Event, EventType, gnu_thread_name};

const PING_PONG_COUNT: usize = 1000;
#[cfg(unix)]
const SEM_POST_COUNT: usize = 500;

/// test_event_init_auto_manual
#[test]
fn event_init_auto_manual() {
    let auto = Event::new(EventType::Auto);
    auto.signal();
    assert!(auto.try_wait());
    assert!(!auto.try_wait());

    let manual = Event::new(EventType::Manual);
    manual.signal();
    assert!(manual.try_wait());
    assert!(manual.try_wait());
}

/// test_event_try_unsignaled
#[test]
fn event_try_unsignaled() {
    assert!(!Event::new(EventType::Auto).try_wait());
    assert!(!Event::new(EventType::Manual).try_wait());
}

/// test_event_auto_semantics
#[test]
fn event_auto_semantics() {
    let ev = Event::new(EventType::Auto);

    ev.signal();
    assert!(ev.try_wait());
    // an auto-reset event is consumed by the first successful try
    assert!(!ev.try_wait());

    // wait returns immediately on a signaled event and consumes it
    ev.signal();
    ev.wait();
    assert!(!ev.try_wait());

    // signaling twice still only releases one waiter
    ev.signal();
    ev.signal();
    assert!(ev.try_wait());
    assert!(!ev.try_wait());

    // reset clears a pending signal
    ev.signal();
    ev.reset();
    assert!(!ev.try_wait());
}

/// test_event_manual_semantics
#[test]
fn event_manual_semantics() {
    let ev = Event::new(EventType::Manual);

    ev.signal();
    // a manual-reset event stays signaled until reset
    assert!(ev.try_wait());
    assert!(ev.try_wait());
    ev.wait();
    assert!(ev.timed_wait(1));
    assert!(ev.try_wait());

    ev.reset();
    assert!(!ev.try_wait());

    // reset on an unsignaled event is harmless
    ev.reset();
    assert!(!ev.try_wait());
}

/// test_event_timedwait
#[test]
fn event_timedwait() {
    let ev = Event::new(EventType::Auto);

    assert!(!ev.timed_wait(10));

    // signaled: returns true immediately and consumes the auto event
    ev.signal();
    assert!(ev.timed_wait(10));
    assert!(!ev.timed_wait(10));

    let ev = Event::new(EventType::Manual);
    assert!(!ev.timed_wait(10));
}

/// test_event_ping_pong
#[test]
fn event_ping_pong() {
    let ping = Arc::new(Event::new(EventType::Auto));
    let pong = Arc::new(Event::new(EventType::Auto));

    let worker = {
        let ping = Arc::clone(&ping);
        let pong = Arc::clone(&pong);
        std::thread::spawn(move || {
            for _ in 0..PING_PONG_COUNT {
                ping.wait();
                pong.signal();
            }
        })
    };

    for _ in 0..PING_PONG_COUNT {
        ping.signal();
        pong.wait();
    }

    worker.join().unwrap();
    assert!(!ping.try_wait());
    assert!(!pong.try_wait());
}

/// test_sem_initial_value
#[cfg(unix)]
#[test]
fn sem_initial_value() {
    let sem = Sem::new(2);

    // two waits do not block with an initial value of 2
    sem.wait();
    sem.wait();
    // and no third credit: initializing as 3 would leave one behind
    assert!(!sem.try_wait());

    // post then wait round-trips, and is again fully consumed
    sem.post();
    sem.wait();
    assert!(!sem.try_wait());
}

/// The native semaphore object must stay at the address where it was
/// initialized: `sem_t` cannot be relocated. `Sem::new` returns a pinned
/// box so safe code cannot move it, and `native_addr` reports the native
/// storage's address — it must lie inside the pinned allocation and not
/// change across posts, waits, or use from another thread.
#[cfg(unix)]
#[test]
fn sem_native_object_never_moves() {
    let sem = Sem::new(1);
    let base = (&*sem as *const Sem) as usize;
    let native = sem.native_addr();
    assert!(
        (base..base + std::mem::size_of::<Sem>()).contains(&native),
        "native storage {native:#x} outside the Sem at {base:#x}"
    );

    assert_eq!(sem.post(), 0);
    assert_eq!(sem.wait(), 0);

    let sem = Arc::new(sem);
    let worker = {
        let sem = Arc::clone(&sem);
        std::thread::spawn(move || {
            sem.post();
            sem.wait();
        })
    };
    worker.join().unwrap();

    assert_eq!(sem.native_addr(), native);
}

/// test_sem_producer_consumer
#[cfg(unix)]
#[test]
fn sem_producer_consumer() {
    let sem = Arc::new(Sem::new(0));

    let producer = {
        let sem = Arc::clone(&sem);
        std::thread::spawn(move || {
            for _ in 0..SEM_POST_COUNT {
                sem.post();
            }
        })
    };

    for _ in 0..SEM_POST_COUNT {
        sem.wait();
    }

    producer.join().unwrap();
}

/// The glibc branch of `os_set_thread_name` truncates to 15 bytes; every
/// other platform passes the name whole (covered in the parity test).
#[test]
fn gnu_thread_name_truncates_at_15() {
    assert_eq!(gnu_thread_name(b"test-thread"), b"test-thread");
    assert_eq!(
        gnu_thread_name(b"a-very-long-thread-name-over-limit"),
        b"a-very-long-thr"
    );
    // exactly 15 is unchanged; 16 loses one byte; empty stays empty
    assert_eq!(gnu_thread_name(b"123456789abcdef"), b"123456789abcdef");
    assert_eq!(gnu_thread_name(b"123456789abcdefg"), b"123456789abcdef");
    assert_eq!(gnu_thread_name(b""), b"");
}

/// Edge case beyond the C test: a manual event stays signalled for every
/// waiter that reaches it, but `signal` wakes only one sleeper at a time
/// (pthread_cond_signal / notify_one), so already-sleeping waiters each
/// need their own signal. An auto event releases exactly one waiter per
/// signal.
#[cfg(unix)]
#[test]
fn event_manual_wakes_all_auto_wakes_one() {
    const WAITERS: usize = 4;

    let ev = Arc::new(Event::new(EventType::Manual));
    let ready = Arc::new(Sem::new(0));
    let done = Arc::new(Sem::new(0));

    for _ in 0..WAITERS {
        let ev = Arc::clone(&ev);
        let ready = Arc::clone(&ready);
        let done = Arc::clone(&done);
        std::thread::spawn(move || {
            ready.post();
            ev.wait();
            done.post();
        });
    }
    for _ in 0..WAITERS {
        ready.wait();
    }

    // The flag stays set, so waiters that have not slept yet pass on
    // entry; one signal per waiter covers any already sleeping.
    for _ in 0..WAITERS {
        ev.signal();
    }
    for _ in 0..WAITERS {
        done.wait();
    }

    let ev = Arc::new(Event::new(EventType::Auto));
    let ready = Arc::new(Sem::new(0));
    let done = Arc::new(Sem::new(0));

    let mut threads = Vec::new();
    for _ in 0..WAITERS {
        let ev = Arc::clone(&ev);
        let ready = Arc::clone(&ready);
        let done = Arc::clone(&done);
        threads.push(std::thread::spawn(move || {
            ready.post();
            ev.wait();
            done.post();
        }));
    }
    for _ in 0..WAITERS {
        ready.wait();
    }

    // one signal releases one waiter; each of the rest needs its own
    for _ in 0..WAITERS {
        ev.signal();
        done.wait();
    }
    for t in threads {
        t.join().unwrap();
    }
}
