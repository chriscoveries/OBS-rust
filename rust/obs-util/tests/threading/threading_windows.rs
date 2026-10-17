//! Tier 1 for `util/threading-windows.c`: the event and semaphore cases in
//! `test/cmocka/test_threading.c`. NULL handles stay in the C shim
//! (`threading_windows_parity`).

#![cfg(windows)]

use std::sync::Arc;
use std::time::{Duration, Instant};

use obs_c_oracle as _; // links the oracle test stubs (blog, bcrash, bmem)
use obs_util::threading::{Event, EventType, Sem};

const PING_PONG_COUNT: usize = 1000;
const SEM_POST_COUNT: usize = 500;

/// test_event_init_auto_manual, through the safe API (destroy of a live
/// event is `Drop`).
#[test]
fn event_init_auto_manual() {
    let auto = Event::new(EventType::Auto);
    auto.signal();
    assert!(auto.try_wait());

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
    assert!(!ev.try_wait());

    ev.signal();
    ev.wait();
    assert!(!ev.try_wait());

    ev.signal();
    ev.signal();
    assert!(ev.try_wait());
    assert!(!ev.try_wait());

    ev.signal();
    ev.reset();
    assert!(!ev.try_wait());
}

/// test_event_manual_semantics
#[test]
fn event_manual_semantics() {
    let ev = Event::new(EventType::Manual);
    ev.signal();
    assert!(ev.try_wait());
    assert!(ev.try_wait());
    ev.wait();
    assert!(ev.timed_wait(1));
    assert!(ev.try_wait());

    ev.reset();
    assert!(!ev.try_wait());
    ev.reset();
    assert!(!ev.try_wait());
}

/// test_event_timedwait
#[test]
fn event_timedwait() {
    let ev = Event::new(EventType::Auto);
    assert!(!ev.timed_wait(10));
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
#[test]
fn sem_initial_value() {
    let sem = Sem::new(2);
    sem.wait();
    sem.wait();
    assert!(!sem.try_wait());
    sem.post();
    sem.wait();
    assert!(!sem.try_wait());
}

/// test_sem_producer_consumer
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

/// One `SetEvent` on a manual-reset event releases every waiter. The C
/// uses `CreateEvent`/`SetEvent`, which keep the event signalled, so a
/// waiter that has not entered the wait yet also passes.
#[cfg(debug_assertions)]
#[test]
fn manual_event_one_signal_wakes_all() {
    const WAITERS: usize = 4;
    let ev = Arc::new(Event::new(EventType::Manual));
    let done = Arc::new(Sem::new(0));
    for _ in 0..WAITERS {
        let ev = Arc::clone(&ev);
        let done = Arc::clone(&done);
        std::thread::spawn(move || {
            ev.wait();
            done.post();
        });
    }
    let deadline = Instant::now() + Duration::from_secs(5);
    while ev.waiting_count() != WAITERS {
        assert!(
            Instant::now() < deadline,
            "waiters did not block: {} of {WAITERS}",
            ev.waiting_count()
        );
        std::thread::yield_now();
    }
    ev.signal();
    for _ in 0..WAITERS {
        done.wait();
    }
}
