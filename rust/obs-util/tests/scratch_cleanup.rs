//! The shared scratch guard must leave nothing behind in the temp dir
//! (obs-rust/obs-studio#769).

mod common;

use common::ScratchDir;
use std::panic::{AssertUnwindSafe, catch_unwind};

const PREFIX: &str = "obs-scratch-cleanup-test";

fn leftovers() -> Vec<String> {
    let mine = format!("{PREFIX}-{}-", std::process::id());
    std::fs::read_dir(std::env::temp_dir())
        .unwrap()
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.starts_with(&mine))
        .collect()
}

/// One test so concurrent tests do not see each other's live directories.
#[test]
fn scratch_dirs_are_removed_on_drop_and_on_panic() {
    assert!(leftovers().is_empty());

    for _ in 0..5 {
        let dir = ScratchDir::new(PREFIX);
        std::fs::create_dir_all(dir.join("nested/deeper")).unwrap();
        std::fs::write(dir.join("nested/deeper/file"), b"x").unwrap();
        assert!(dir.is_dir());
    }
    assert!(
        leftovers().is_empty(),
        "leaked after drop: {:?}",
        leftovers()
    );

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dir = ScratchDir::new(PREFIX);
        std::fs::write(dir.join("file"), b"x").unwrap();
        panic!("simulated failing proptest case");
    }));
    assert!(result.is_err());
    assert!(
        leftovers().is_empty(),
        "leaked after panic: {:?}",
        leftovers()
    );
}
