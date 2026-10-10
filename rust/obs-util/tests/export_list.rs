use obs_c_oracle as _;

use std::collections::BTreeSet;
use std::fs;

/// Names of fns following a `#[unsafe(no_mangle)]` / `#[no_mangle]` attribute.
fn no_mangle_fns(src: &str) -> Vec<String> {
    let lines: Vec<&str> = src.lines().collect();
    let mut out = Vec::new();
    for (i, line) in lines.iter().enumerate() {
        let t = line.trim();
        if t != "#[unsafe(no_mangle)]" && t != "#[no_mangle]" {
            continue;
        }
        for next in &lines[i + 1..] {
            if let Some(pos) = next.find("fn ") {
                let name: String = next[pos + 3..]
                    .trim_start()
                    .chars()
                    .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
                    .collect();
                out.push(name);
                break;
            }
        }
    }
    out
}

fn symbol_list(rel: &str) -> BTreeSet<String> {
    let text = fs::read_to_string(
        concat!(env!("CARGO_MANIFEST_DIR"), "/../../libobs/cmake/").to_owned() + rel,
    )
    .unwrap_or_else(|_| panic!("read {rel}"));
    text.lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(String::from)
        .collect()
}

#[test]
fn rust_exports_list_matches_no_mangle_shims() {
    let listed = symbol_list("rust-exports.txt");
    let hidden = symbol_list("rust-hidden.txt");
    let overlap: Vec<_> = listed.intersection(&hidden).collect();
    assert!(
        overlap.is_empty(),
        "symbol is both exported and hidden: {overlap:?}"
    );

    let mut found = BTreeSet::new();
    let mut visited = Vec::new();
    for krate in fs::read_dir(concat!(env!("CARGO_MANIFEST_DIR"), "/..")).expect("read rust/") {
        let ffi_dir = krate.expect("dir entry").path().join("src/ffi");
        if !ffi_dir.is_dir() {
            continue;
        }
        for entry in fs::read_dir(&ffi_dir).expect("read src/ffi") {
            let path = entry.expect("dir entry").path();
            if path.extension().is_some_and(|e| e == "rs") {
                let src = fs::read_to_string(&path).expect("read ffi source");
                found.extend(no_mangle_fns(&src));
            }
        }
        visited.push(ffi_dir);
    }
    for krate in ["obs-util", "obs-graphics", "obs-media-io"] {
        assert!(
            visited
                .iter()
                .any(|d| d.ends_with(format!("{krate}/src/ffi"))),
            "{krate}/src/ffi was not scanned: {visited:?}"
        );
    }

    let mut accounted = listed.clone();
    accounted.extend(hidden.iter().cloned());
    let missing_from_list: Vec<_> = found.difference(&accounted).collect();
    let missing_from_src: Vec<_> = accounted.difference(&found).collect();
    assert!(
        missing_from_list.is_empty() && missing_from_src.is_empty(),
        "shims missing from rust-exports.txt and rust-hidden.txt: {missing_from_list:?}; \
         listed but no #[no_mangle] shim in rust/*/src/ffi:{missing_from_src:?}"
    );

    let map = fs::read_to_string(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../libobs/cmake/rust-exports.map"
    ))
    .expect("read rust-exports.map");
    let macos = fs::read_to_string(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../libobs/cmake/rust-unexports-macos.txt"
    ))
    .expect("read rust-unexports-macos.txt");
    for name in &hidden {
        assert!(
            map.contains(&format!("{name};")),
            "{name} is hidden but rust-exports.map does not list it as local"
        );
        assert!(
            macos.lines().any(|line| line.trim() == format!("_{name}")),
            "{name} is hidden but rust-unexports-macos.txt has no _{name}"
        );
    }
}
