//! Tier 1: safe-core tests. Names follow `test/cmocka/test_base.c`.
//!
//! The safe API takes an already-formatted message. printf formatting and the
//! default handler's stdio output are checked in `base_parity`.

use std::cell::RefCell;
use std::rc::Rc;

use obs_c_oracle as _;
use obs_util::base::{
    CRASHED_IN_CRASH_HANDLER, CrashInstall, LOG_DEBUG, LOG_ERROR, LOG_INFO, LOG_LINE_CAP,
    LOG_WARNING, LogStream, Logger, default_log_format,
};

struct Rec {
    calls: i32,
    level: i32,
    message: String,
}

fn same_fn<P>(left: Option<fn(i32, &str, &P)>, right: Option<fn(i32, &str, &P)>) -> bool {
    match (left, right) {
        (None, None) => true,
        (Some(a), Some(b)) => std::ptr::fn_addr_eq(a, b),
        _ => false,
    }
}

fn record(level: i32, message: &str, rec: &Rc<RefCell<Rec>>) {
    let mut rec = rec.borrow_mut();
    rec.calls += 1;
    rec.level = level;
    rec.message.clear();
    rec.message.push_str(message);
}

fn blank() -> Rc<RefCell<Rec>> {
    Rc::new(RefCell::new(Rec {
        calls: 0,
        level: 0,
        message: String::new(),
    }))
}

#[test]
fn blog_delivers_level_and_message_test() {
    let rec = blank();
    let mut log = Logger::new(Rc::clone(&rec));
    let prev = log.log_handler();
    assert!(prev.0.is_none());

    log.set_log_handler(Some(record), Rc::clone(&rec));
    log.blog(LOG_INFO, "x=5");
    assert_eq!(rec.borrow().calls, 1);
    assert_eq!(rec.borrow().level, LOG_INFO);
    assert_eq!(rec.borrow().message, "x=5");

    log.blog(LOG_WARNING, "a-b");
    assert_eq!(rec.borrow().calls, 2);
    assert_eq!(rec.borrow().level, LOG_WARNING);
    assert_eq!(rec.borrow().message, "a-b");

    log.blog(LOG_ERROR, "plain");
    assert_eq!(rec.borrow().calls, 3);
    assert_eq!(rec.borrow().level, LOG_ERROR);
    assert_eq!(rec.borrow().message, "plain");

    log.set_log_handler(prev.0, prev.1);
}

#[test]
fn get_log_handler_test() {
    fn ignore(_level: i32, _message: &str, _param: &Rc<()>) {}

    let marker = Rc::new(());
    let mut log = Logger::new(Rc::new(()));
    let prev = log.log_handler();

    log.set_log_handler(Some(ignore), Rc::clone(&marker));
    let (cur, cur_param) = log.log_handler();
    assert!(same_fn(cur, Some(ignore)));
    assert!(Rc::ptr_eq(&cur_param, &marker));

    log.set_log_handler(None, Rc::clone(&marker));
    let (cur, cur_param) = log.log_handler();
    assert!(cur.is_none());
    assert!(!same_fn(cur, Some(ignore)));
    assert!(Rc::ptr_eq(&cur_param, &marker));

    log.set_log_handler(prev.0, prev.1.clone());
    let (cur, cur_param) = log.log_handler();
    assert!(same_fn(cur, prev.0));
    assert!(Rc::ptr_eq(&cur_param, &prev.1));
}

#[test]
fn blogva_test() {
    let rec = blank();
    let mut log = Logger::new(Rc::clone(&rec));
    let prev = log.log_handler();
    log.set_log_handler(Some(record), Rc::clone(&rec));

    log.blog(LOG_DEBUG, "id:007");
    assert_eq!(rec.borrow().calls, 1);
    assert_eq!(rec.borrow().level, LOG_DEBUG);
    assert_eq!(rec.borrow().message, "id:007");

    log.set_log_handler(prev.0, prev.1);
}

fn crash_ignore(_message: &str, _param: &Rc<RefCell<Rec>>) {}

#[test]
fn set_crash_handler_once_test() {
    let rec = blank();
    let mut log = Logger::new(Rc::clone(&rec));
    let prev = log.log_handler();
    log.set_log_handler(Some(record), Rc::clone(&rec));

    assert_eq!(
        log.install_crash_handler(crash_ignore, Rc::clone(&rec)),
        CrashInstall::Installed
    );
    assert_eq!(rec.borrow().calls, 0);

    assert_eq!(
        log.install_crash_handler(crash_ignore, Rc::clone(&rec)),
        CrashInstall::AlreadyInstalled
    );
    assert_eq!(rec.borrow().calls, 1);
    assert_eq!(rec.borrow().level, LOG_WARNING);
    assert_eq!(
        rec.borrow().message,
        "Tried to set a crash handler when one already exists."
    );
    assert!(log.crash_handler().0.is_some_and(|handler| {
        std::ptr::fn_addr_eq(handler, crash_ignore as fn(&str, &Rc<RefCell<Rec>>))
    }));

    log.set_log_handler(prev.0, prev.1);
}

#[test]
fn begin_crash_is_one_shot() {
    let rec = blank();
    let mut log = Logger::new(Rc::clone(&rec));
    assert!(log.begin_crash().is_some());
    assert!(log.begin_crash().is_none());
}

#[test]
fn default_log_format_matches_c_switch() {
    assert_eq!(
        default_log_format(LOG_DEBUG),
        Some(("debug: %s\n", LogStream::Stdout))
    );
    assert_eq!(
        default_log_format(LOG_INFO),
        Some(("info: %s\n", LogStream::Stdout))
    );
    assert_eq!(
        default_log_format(LOG_WARNING),
        Some(("warning: %s\n", LogStream::Stdout))
    );
    assert_eq!(
        default_log_format(LOG_ERROR),
        Some(("error: %s\n", LogStream::Stderr))
    );
    assert_eq!(default_log_format(0), None);
    assert_eq!(default_log_format(150), None);
    assert_eq!(LOG_LINE_CAP, 8192);
}

#[test]
fn reentry_message_is_the_c_adapter_string() {
    let src = include_str!("../../../../libobs/util/base-variadic.c.h");
    assert!(src.contains(CRASHED_IN_CRASH_HANDLER));
}
