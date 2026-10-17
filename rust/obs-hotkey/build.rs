//! Generates the key table from `libobs/obs-hotkeys.h`, the list the C
//! expands with `OBS_HOTKEY(x)` (and `OBS_MOUSE_BUTTON(x)`), so the Rust names cannot drift from it.

use std::fmt::Write as _;
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let header = manifest.join("../../libobs/obs-hotkeys.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let text = std::fs::read_to_string(&header).expect("read obs-hotkeys.h");
    let mut out = String::from(
        "/// `obs-hotkeys.h` in enum order.\npub(crate) static KEY_NAMES: &[&std::ffi::CStr] = &[\n",
    );
    for line in text.lines() {
        let line = line.trim();
        // OBS_MOUSE_BUTTON(x) expands to OBS_HOTKEY(x) unless the includer
        // defines it, and obs-hotkey-name-map.c does not.
        let rest = line
            .strip_prefix("OBS_HOTKEY(")
            .or_else(|| line.strip_prefix("OBS_MOUSE_BUTTON("));
        if let Some(rest) = rest {
            let name = rest.strip_suffix(')').expect("OBS_HOTKEY(x) on one line");
            writeln!(out, "    c\"{name}\",").unwrap();
        }
    }
    out.push_str("];\n");
    let dest = PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("keys.rs");
    std::fs::write(dest, out).expect("write keys.rs");
}
