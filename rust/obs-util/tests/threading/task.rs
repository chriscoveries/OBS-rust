//! Tier 1: 1:1 Rust port of `test/cmocka/test_task.c` plus edge cases.
//!
//! Deterministic: no sleeps; synchronization only through `TaskQueue::wait`,
//! manual-reset events (mutex + condvar) and thread joins.
//!
//! The `test_task_null_args` case is not ported here: the safe API has no
//! nullable queue handle. The NULL checks live in the C shim and are covered
//! by the unchanged C test (Tier 2) and the parity test (Tier 3).
//!
//! Note on `wait`'s return value, mirroring the C test's header comment: it
//! is true only if a task other than the wait marker ran after the wait call
//! set its `waiting` flag. Whether earlier tasks are still pending at that
//! moment is a race, so the true case is not asserted (characterized, not
//! endorsed). The false case on an idle queue is deterministic and is pinned.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)
use obs_util::task::TaskQueue;

const NUM_TASKS: usize = 100;

/// Manual-reset event for test synchronization.
struct Event {
    state: Mutex<bool>,
    cond: Condvar,
}

impl Event {
    fn new() -> Self {
        Self {
            state: Mutex::new(false),
            cond: Condvar::new(),
        }
    }

    fn signal(&self) {
        *self.state.lock().unwrap() = true;
        self.cond.notify_all();
    }

    fn wait(&self) {
        let mut set = self.state.lock().unwrap();
        while !*set {
            set = self.cond.wait(set).unwrap();
        }
    }
}

#[test]
fn task_create_destroy_idle() {
    let tq = TaskQueue::new().unwrap();
    drop(tq);
}

#[test]
fn task_fifo_order() {
    let order = Arc::new(Mutex::new(Vec::new()));
    let tq = TaskQueue::new().unwrap();

    for i in 0..NUM_TASKS {
        let order = Arc::clone(&order);
        assert!(tq.queue_task(Box::new(move || order.lock().unwrap().push(i))));
    }

    // the return value is racy (see header comment); only ordering is pinned
    let _ = tq.wait();

    let order = order.lock().unwrap();
    assert_eq!(order.len(), NUM_TASKS);
    for (i, got) in order.iter().enumerate() {
        assert_eq!(*got, i);
    }
}

#[test]
fn task_wait_idle_returns_false() {
    let tq = TaskQueue::new().unwrap();

    // nothing queued: only the wait marker runs, so no task was "processed"
    assert!(!tq.wait());
    assert!(!tq.wait());
}

#[test]
fn task_wait_runs_earlier_tasks() {
    let order = Arc::new(Mutex::new(Vec::new()));
    let tq = TaskQueue::new().unwrap();

    for i in [1, 2] {
        let order = Arc::clone(&order);
        assert!(tq.queue_task(Box::new(move || order.lock().unwrap().push(i))));
    }
    let _ = tq.wait();

    assert_eq!(*order.lock().unwrap(), vec![1, 2]);

    // a second wait with an idle queue returns false
    assert!(!tq.wait());
    assert_eq!(order.lock().unwrap().len(), 2);
}

#[test]
fn task_inside() {
    let tq = Arc::new(TaskQueue::new().unwrap());
    let other = Arc::new(TaskQueue::new().unwrap());

    // the test thread is not a worker of either queue
    assert!(!tq.inside());
    assert!(!other.inside());

    let results = Arc::new(Mutex::new((false, true)));
    {
        let tq = Arc::clone(&tq);
        let other = Arc::clone(&other);
        let results = Arc::clone(&results);
        assert!(tq.clone().queue_task(Box::new(move || {
            *results.lock().unwrap() = (tq.inside(), other.inside());
        })));
    }
    let _ = tq.wait();

    let (inside_own, inside_other) = *results.lock().unwrap();
    assert!(inside_own);
    // a worker of one queue is not inside another queue
    assert!(!inside_other);
}

#[test]
fn task_queue_from_task() {
    let order = Arc::new(Mutex::new(Vec::new()));
    let tq = Arc::new(TaskQueue::new().unwrap());

    {
        let tq = Arc::clone(&tq);
        let order = Arc::clone(&order);
        assert!(tq.clone().queue_task(Box::new(move || {
            // queuing from inside a task on the same queue does not deadlock
            let order_inner = Arc::clone(&order);
            assert!(tq.queue_task(Box::new(move || {
                order_inner.lock().unwrap().push(20);
            })));
            order.lock().unwrap().push(10);
        })));
    }

    /*
     * The first wait may return before the nested task is queued, so the
     * nested task is flushed by a second wait (FIFO guarantees it ran).
     * Both waits' return values are racy and ignored.
     */
    let _ = tq.wait();
    let _ = tq.wait();

    // the nested task runs after the task that queued it
    assert_eq!(*order.lock().unwrap(), vec![10, 20]);
}

#[test]
fn task_destroy_runs_pending() {
    let started = Arc::new(Event::new());
    let gate = Arc::new(Event::new());
    let counter = Arc::new(AtomicUsize::new(0));

    let tq = TaskQueue::new().unwrap();

    {
        let started = Arc::clone(&started);
        let gate = Arc::clone(&gate);
        assert!(tq.queue_task(Box::new(move || {
            started.signal();
            gate.wait();
        })));
    }
    // the worker is now provably inside the blocking task
    started.wait();

    for _ in 0..5 {
        let counter = Arc::clone(&counter);
        assert!(tq.queue_task(Box::new(move || {
            counter.fetch_add(1, Ordering::SeqCst);
        })));
    }

    assert_eq!(counter.load(Ordering::SeqCst), 0);

    gate.signal();
    drop(tq);

    /*
     * Characterized, not endorsed: destroy does not discard pending tasks.
     * The stop marker is queued behind them and the worker is joined, so
     * all of them run before destroy returns.
     */
    assert_eq!(counter.load(Ordering::SeqCst), 5);
}

// --- edge cases beyond the C test ---

/// Two queues run their tasks independently, each in FIFO order.
#[test]
fn task_queues_are_independent() {
    let a_log = Arc::new(Mutex::new(Vec::new()));
    let b_log = Arc::new(Mutex::new(Vec::new()));

    let a = TaskQueue::new().unwrap();
    let b = TaskQueue::new().unwrap();

    for i in 0..8 {
        let (a_log, b_log) = (Arc::clone(&a_log), Arc::clone(&b_log));
        assert!(a.queue_task(Box::new(move || a_log.lock().unwrap().push(i))));
        assert!(b.queue_task(Box::new(move || b_log.lock().unwrap().push(i * 10))));
    }

    let _ = a.wait();
    let _ = b.wait();

    assert_eq!(*a_log.lock().unwrap(), (0..8).collect::<Vec<_>>());
    assert_eq!(
        *b_log.lock().unwrap(),
        (0..8).map(|i| i * 10).collect::<Vec<_>>()
    );
}

/// Tasks queued concurrently from several producer threads all run exactly
/// once. Order across producers is not pinned; the count is.
#[test]
fn task_many_producers() {
    const PRODUCERS: usize = 4;
    const PER_PRODUCER: usize = 64;

    let counter = Arc::new(AtomicUsize::new(0));
    let tq = Arc::new(TaskQueue::new().unwrap());

    let producers: Vec<_> = (0..PRODUCERS)
        .map(|_| {
            let tq = Arc::clone(&tq);
            let counter = Arc::clone(&counter);
            std::thread::spawn(move || {
                for _ in 0..PER_PRODUCER {
                    let counter = Arc::clone(&counter);
                    assert!(tq.queue_task(Box::new(move || {
                        counter.fetch_add(1, Ordering::SeqCst);
                    })));
                }
            })
        })
        .collect();
    for p in producers {
        p.join().unwrap();
    }

    // one wait guarantees every queued task ran (the wait marker is FIFO last)
    let _ = tq.wait();
    assert_eq!(counter.load(Ordering::SeqCst), PRODUCERS * PER_PRODUCER);
}

/// A task that queues another task onto a *different* queue does not stall
/// either worker.
#[test]
fn task_cross_queue_nested() {
    let order = Arc::new(Mutex::new(Vec::new()));
    let a = Arc::new(TaskQueue::new().unwrap());
    let b = Arc::new(TaskQueue::new().unwrap());

    {
        let b = Arc::clone(&b);
        let order = Arc::clone(&order);
        assert!(a.queue_task(Box::new(move || {
            let order_inner = Arc::clone(&order);
            assert!(b.queue_task(Box::new(move || {
                order_inner.lock().unwrap().push(2);
            })));
            order.lock().unwrap().push(1);
        })));
    }

    let _ = a.wait();
    let _ = b.wait();

    let order = order.lock().unwrap();
    assert!(order.contains(&1) && order.contains(&2));
}
