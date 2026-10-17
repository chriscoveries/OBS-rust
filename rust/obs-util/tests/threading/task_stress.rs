//! Stress test for the task queue, required by the Phase 4 rules on top of
//! Tier 3 parity ("the threading and task ports add a loom or stress-style
//! Rust test").
//!
//! Determinism: every assertion is on counts or ordering established through
//! `wait` and thread joins — never on sleeps or timing.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use obs_c_oracle as _; // links the test bmalloc/bfree (oracle/test_bmem.c)
use obs_util::task::TaskQueue;

/// Many producers hammering many queues; every queued task runs exactly
/// once, each queue in FIFO order per producer.
#[test]
fn stress_multi_queue_multi_producer() {
    const QUEUES: usize = 4;
    const PRODUCERS: usize = 4;
    const TASKS_PER_PRODUCER: usize = 128;

    let total = Arc::new(AtomicUsize::new(0));

    for _ in 0..QUEUES {
        let tq = Arc::new(TaskQueue::new().unwrap());
        // Per-producer sequence numbers: each producer's tasks must land in
        // the queue's log in order, though producers may interleave.
        let logs: Vec<Arc<Mutex<Vec<usize>>>> = (0..PRODUCERS)
            .map(|_| Arc::new(Mutex::new(Vec::new())))
            .collect();

        let producers: Vec<_> = (0..PRODUCERS)
            .map(|p| {
                let tq = Arc::clone(&tq);
                let total = Arc::clone(&total);
                let log = Arc::clone(&logs[p]);
                std::thread::spawn(move || {
                    for i in 0..TASKS_PER_PRODUCER {
                        let total = Arc::clone(&total);
                        let log = Arc::clone(&log);
                        assert!(tq.queue_task(Box::new(move || {
                            log.lock().unwrap().push(i);
                            total.fetch_add(1, Ordering::SeqCst);
                        })));
                    }
                })
            })
            .collect();
        for p in producers {
            p.join().unwrap();
        }

        let _ = tq.wait();
        for log in &logs {
            let log = log.lock().unwrap();
            assert_eq!(log.len(), TASKS_PER_PRODUCER);
            assert!(log.windows(2).all(|w| w[0] < w[1]));
        }
    }

    assert_eq!(
        total.load(Ordering::SeqCst),
        QUEUES * PRODUCERS * TASKS_PER_PRODUCER
    );
}

/// Rapid create/queue/destroy churn: destroy never drops a queued task and
/// never deadlocks.
#[test]
fn stress_create_destroy_churn() {
    const ROUNDS: usize = 64;

    for _ in 0..ROUNDS {
        let counter = Arc::new(AtomicUsize::new(0));
        let tq = TaskQueue::new().unwrap();
        for _ in 0..16 {
            let counter = Arc::clone(&counter);
            assert!(tq.queue_task(Box::new(move || {
                counter.fetch_add(1, Ordering::SeqCst);
            })));
        }
        drop(tq);
        assert_eq!(counter.load(Ordering::SeqCst), 16);
    }
}

/// Tasks that queue more tasks (a chain) resolve fully before the queue is
/// idle again.
#[test]
fn stress_nested_chains() {
    const CHAINS: usize = 32;
    const DEPTH: usize = 32;

    let counter = Arc::new(AtomicUsize::new(0));
    let tq = Arc::new(TaskQueue::new().unwrap());

    fn chain(tq: &Arc<TaskQueue>, counter: &Arc<AtomicUsize>, depth: usize) {
        let inner = Arc::clone(tq);
        let counter = Arc::clone(counter);
        assert!(tq.queue_task(Box::new(move || {
            counter.fetch_add(1, Ordering::SeqCst);
            if depth > 0 {
                chain(&inner, &counter, depth - 1);
            }
        })));
    }

    for _ in 0..CHAINS {
        chain(&tq, &counter, DEPTH);
    }

    // Repeated waits flush arbitrarily deep nested chains.
    for _ in 0..DEPTH + 2 {
        let _ = tq.wait();
    }
    assert_eq!(counter.load(Ordering::SeqCst), CHAINS * (DEPTH + 1));
}
