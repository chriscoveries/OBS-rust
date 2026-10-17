//! Stress test for the event and semaphore, required by the Phase 4 rules
//! on top of Tier 3 parity ("the threading and task ports add a loom or
//! stress-style Rust test").
//!
//! Determinism: every assertion is on counts established through sem waits
//! and thread joins — never on sleeps or timing.

use std::sync::Arc;
#[cfg(unix)]
use std::sync::atomic::{AtomicUsize, Ordering};

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)
#[cfg(unix)]
use obs_util::threading::Sem;
use obs_util::threading::{Event, EventType};

/// Ping-pong pairs of auto events across many thread pairs: every volley
/// must complete exactly once per direction.
#[test]
fn stress_event_ping_pong_pairs() {
    const PAIRS: usize = 8;
    const ROUNDS: usize = 500;

    let mut threads = Vec::new();
    for _ in 0..PAIRS {
        let ping = Arc::new(Event::new(EventType::Auto));
        let pong = Arc::new(Event::new(EventType::Auto));
        let worker = {
            let ping = Arc::clone(&ping);
            let pong = Arc::clone(&pong);
            std::thread::spawn(move || {
                for _ in 0..ROUNDS {
                    ping.wait();
                    pong.signal();
                }
            })
        };
        threads.push(std::thread::spawn(move || {
            for _ in 0..ROUNDS {
                ping.signal();
                pong.wait();
            }
            worker.join().unwrap();
        }));
    }
    for t in threads {
        t.join().unwrap();
    }
}

/// One manual event, many waiters: liveness only. Every waiter must be
/// released eventually — no deadlock, no lost signal. The exact
/// one-wake-per-signal property is asserted where it is exact, in the
/// `signal_dispatches_notify_one` unit test via `Event`'s notify seam;
/// under real concurrency it cannot be, since a manual event's flag
/// stays set and a POSIX-legal spurious wakeup also releases a waiter.
///
/// Debug-builds only: `Event::waiting_count` is `#[cfg(debug_assertions)]`
/// in the library, so this test cannot link against a release build.
///
/// Determinism: each signal is gated on `Event::waiting_count` (a
/// debug-build counter incremented once a thread is inside the condvar
/// wait) reaching the not-yet-released count, so every remaining waiter
/// is provably asleep before each signal — no settle window.
#[cfg(unix)]
#[cfg(debug_assertions)]
#[test]
fn stress_manual_event_releases_all_waiters() {
    const WAITERS: usize = 32;

    let ev = Arc::new(Event::new(EventType::Manual));
    let released = Arc::new(AtomicUsize::new(0));
    let done = Arc::new(Sem::new(0));

    let threads: Vec<_> = (0..WAITERS)
        .map(|_| {
            let ev = Arc::clone(&ev);
            let released = Arc::clone(&released);
            let done = Arc::clone(&done);
            std::thread::spawn(move || {
                ev.wait();
                released.fetch_add(1, Ordering::SeqCst);
                done.post();
            })
        })
        .collect();

    // Each iteration holds the manual flag until at least one release is
    // confirmed, then resets and drains one `done` post per released
    // waiter, so releasers cannot straddle iterations. The blocked +
    // released == WAITERS invariant self-corrects for a releaser lagging
    // on its counter. One signal always releases at least one waiter, so
    // the bounded loop always converges; a lost signal or a missed wake
    // stalls the loop and fails on the deadline rather than hanging.
    let mut posts = 0;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(60);
    loop {
        let r = released.load(Ordering::SeqCst);
        if r == WAITERS {
            break;
        }
        assert!(
            std::time::Instant::now() < deadline,
            "manual event stalled: {r} released, {} blocked, want {WAITERS} released",
            ev.waiting_count()
        );
        if ev.waiting_count() != WAITERS - r {
            std::thread::yield_now();
            continue;
        }
        ev.signal();
        done.wait();
        posts += 1;
        ev.reset();
        loop {
            let now = released.load(Ordering::SeqCst);
            while posts < now {
                done.wait();
                posts += 1;
            }
            if released.load(Ordering::SeqCst) == now {
                break;
            }
        }
    }

    for t in threads {
        t.join().unwrap();
    }
    assert_eq!(released.load(Ordering::SeqCst), WAITERS);
}

/// Many producers on one counting semaphore; every posted count is taken
/// exactly once.
#[cfg(unix)]
#[test]
fn stress_sem_multi_producer() {
    const PRODUCERS: usize = 8;
    const POSTS: usize = 500;

    let sem = Arc::new(Sem::new(0));
    let consumed = Arc::new(AtomicUsize::new(0));

    let producers: Vec<_> = (0..PRODUCERS)
        .map(|_| {
            let sem = Arc::clone(&sem);
            std::thread::spawn(move || {
                for _ in 0..POSTS {
                    sem.post();
                }
            })
        })
        .collect();

    let consumer = {
        let sem = Arc::clone(&sem);
        let consumed = Arc::clone(&consumed);
        std::thread::spawn(move || {
            for _ in 0..PRODUCERS * POSTS {
                sem.wait();
                consumed.fetch_add(1, Ordering::SeqCst);
            }
        })
    };

    for p in producers {
        p.join().unwrap();
    }
    consumer.join().unwrap();
    assert_eq!(consumed.load(Ordering::SeqCst), PRODUCERS * POSTS);
}

/// reset() under contention: the waiter must observe a signal — a
/// timed_wait that always timed out must not pass — and must never
/// observe a signal that was reset before it consumed it.
// Uses `Sem` (POSIX only) for its start handshake.
#[cfg(unix)]
#[test]
fn stress_event_signal_reset_contention() {
    const ROUNDS: usize = 2000;
    /// Only bounds a genuinely broken timed_wait; on a correct build the
    /// ack arrives as soon as the waiter is scheduled, which on a loaded
    /// shared runner can already take seconds.
    const ACK_MS: u64 = 60_000;

    let ev = Arc::new(Event::new(EventType::Manual));
    let ack = Arc::new(Event::new(EventType::Auto));
    let started = Arc::new(Sem::new(0));
    let observed = Arc::new(AtomicUsize::new(0));
    let stop = Arc::new(AtomicUsize::new(0));

    // The waiter must stay in its wait loop until the toggler is done:
    // `while ev.timed_wait(1)` exits on a 1 ms timeout that lands before
    // the first signal, so a once-running waiter is not a still-waiting
    // one. Poll `stop` on each timeout instead; the manual flag stays
    // set until the toggler's first reset, so the first observation and
    // the ack are guaranteed once the waiter runs.
    let toggler = {
        let ev = Arc::clone(&ev);
        let ack = Arc::clone(&ack);
        let started = Arc::clone(&started);
        let stop = Arc::clone(&stop);
        std::thread::spawn(move || {
            started.wait();
            ev.signal();
            assert!(ack.timed_wait(ACK_MS), "waiter never observed a signal");
            for _ in 0..ROUNDS {
                ev.signal();
                ev.reset();
            }
            stop.store(1, Ordering::SeqCst);
            // Wake the waiter out of its current timed_wait promptly,
            // then leave the flag reset for the terminal assertion.
            ev.signal();
            ev.reset();
        })
    };

    let waiter = {
        let ev = Arc::clone(&ev);
        let ack = Arc::clone(&ack);
        let started = Arc::clone(&started);
        let observed = Arc::clone(&observed);
        let stop = Arc::clone(&stop);
        std::thread::spawn(move || {
            started.post();
            while stop.load(Ordering::SeqCst) == 0 {
                if ev.timed_wait(50) && observed.fetch_add(1, Ordering::SeqCst) == 0 {
                    ack.signal();
                }
            }
        })
    };

    toggler.join().unwrap();
    waiter.join().unwrap();
    // `observed` is intentionally unbounded: while the manual flag stays
    // set, every timed_wait returns true, so sightings are not limited by
    // ROUNDS. What is deterministic is that at least one signal arrived
    // in time (the handshake) and the terminal state: the toggler's last
    // op was a reset, both threads joined, so the flag must read cleared
    // and the waiter must have left the loop rather than deadlocking on
    // a stale signal.
    assert!(
        observed.load(Ordering::SeqCst) > 0,
        "timed_wait never observed a signal"
    );
    assert!(
        !ev.try_wait(),
        "flag must be reset after the toggler joined (observed={})",
        observed.load(Ordering::SeqCst)
    );
}
