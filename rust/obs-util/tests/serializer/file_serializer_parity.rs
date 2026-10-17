//! Tier 3: the file-serializer C ABI against the original C, compiled as an
//! oracle.
//!
//! Exclusions, not generated here:
//! - A null `path` to `file_output_serializer_init_safe`. Rust returns false
//!   and creates nothing. C builds a temp name from the extension alone
//!   (a relative file in the process cwd) and `os_unlink(NULL)` on free.
//! - An empty `path` to `init_safe`. Both sides would create a relative
//!   temp name in the process cwd.
//! - A null data pointer with size > 0. `fwrite` of that is undefined. The
//!   shim returns 0, matching `s_write` in `serializer.h`.
//! - A null serializer pointer. `free` returns. C would crash. The cmocka
//!   tests always pass a real serializer.
//! - Windows only, input: the stream state after a failed seek (one that
//!   would land before the start of the file). Both sides must return -1,
//!   but the MSVC CRT's failed `_fseeki64` leaves `ftell` reporting how far
//!   its read-ahead buffer got instead of the unchanged position, so the
//!   comparison stops there. Rust keeps the position, as glibc does.
//!
//! `struct serializer` layout is already checked by the array-serializer
//! tests. This port adds no new `#[repr(C)]` struct.
//!
//! Reminder: the mutation check (break the core, see this file fail) is done
//! once per port, not on every change.

use core::ffi::{c_int, c_void};
use std::ffi::CString;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use obs_c_oracle::array_serializer::OracleSerializer;
use obs_c_oracle::file_serializer::{
    oracle_file_input_serializer_free, oracle_file_input_serializer_init,
    oracle_file_output_serializer_free, oracle_file_output_serializer_init,
    oracle_file_output_serializer_init_safe,
};
use obs_util::ffi::array_serializer::serializer;
use obs_util::ffi::file_serializer::{
    file_input_serializer_free, file_input_serializer_init, file_output_serializer_free,
    file_output_serializer_init, file_output_serializer_init_safe,
};
use obs_util::file_serializer::temp_file_name;
use proptest::prelude::*;

#[derive(Debug, Clone)]
enum Op {
    Write(Vec<u8>),
    Read(usize),
    Seek(i64, c_int),
    Pos,
}

#[derive(Debug, PartialEq, Eq)]
struct Outcome {
    ret: i64,
    pos: i64,
    bytes: Vec<u8>,
}

type ReadFn = unsafe extern "C" fn(*mut c_void, *mut c_void, usize) -> usize;
type WriteFn = unsafe extern "C" fn(*mut c_void, *const c_void, usize) -> usize;
type SeekFn = unsafe extern "C" fn(*mut c_void, i64, c_int) -> i64;
type PosFn = unsafe extern "C" fn(*mut c_void) -> i64;

struct View {
    data: *mut c_void,
    read: Option<ReadFn>,
    write: Option<WriteFn>,
    seek: Option<SeekFn>,
    get_pos: Option<PosFn>,
}

fn pos_of(view: &View) -> i64 {
    // SAFETY: `get_pos` was installed by a successful init and `data` is the
    // live file state.
    unsafe { (view.get_pos.unwrap())(view.data) }
}

fn run(view: &View, op: &Op) -> Outcome {
    match op {
        Op::Write(buf) => {
            // SAFETY: `write` was installed by output init. `buf` is valid
            // for its length. Size 0 does not read `buf`.
            let n = unsafe { (view.write.unwrap())(view.data, buf.as_ptr().cast(), buf.len()) };
            Outcome {
                ret: n as i64,
                pos: pos_of(view),
                bytes: Vec::new(),
            }
        }
        Op::Read(n) => {
            let mut buf = vec![0u8; *n];
            // SAFETY: `read` was installed by input init. `buf` covers `n`.
            let got = unsafe { (view.read.unwrap())(view.data, buf.as_mut_ptr().cast(), *n) };
            buf.truncate(got);
            Outcome {
                ret: got as i64,
                pos: pos_of(view),
                bytes: buf,
            }
        }
        Op::Seek(offset, kind) => {
            // SAFETY: `seek` was installed by init. `data` is the live file.
            let ret = unsafe { (view.seek.unwrap())(view.data, *offset, *kind) };
            Outcome {
                ret,
                pos: pos_of(view),
                bytes: Vec::new(),
            }
        }
        Op::Pos => {
            let ret = pos_of(view);
            Outcome {
                ret,
                pos: ret,
                bytes: Vec::new(),
            }
        }
    }
}

fn path_bytes(path: &Path) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        path.as_os_str().as_bytes().to_vec()
    }
    #[cfg(not(unix))]
    {
        path.to_str().unwrap().as_bytes().to_vec()
    }
}

fn path_from_bytes(bytes: &[u8]) -> PathBuf {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        PathBuf::from(std::ffi::OsStr::from_bytes(bytes))
    }
    #[cfg(not(unix))]
    {
        // libobs utf8_to_wchar substitutes U+FFFD for invalid UTF-8.
        PathBuf::from(String::from_utf8_lossy(bytes).as_ref())
    }
}

fn temp_of(path: &Path, ext: &str) -> PathBuf {
    path_from_bytes(&temp_file_name(&path_bytes(path), ext.as_bytes()))
}

fn c_path(path: &Path) -> CString {
    CString::new(path_bytes(path)).unwrap()
}

struct Scratch(PathBuf);

impl Scratch {
    fn new() -> Self {
        static N: AtomicU64 = AtomicU64::new(0);
        let n = N.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "obs-file-serializer-parity-{}-{}-{n}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        Self(dir)
    }

    fn join(&self, name: &str) -> PathBuf {
        self.0.join(name)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn empty_rust() -> serializer {
    serializer {
        data: core::ptr::null_mut(),
        read: None,
        write: None,
        seek: None,
        get_pos: None,
    }
}

fn empty_oracle() -> OracleSerializer {
    OracleSerializer {
        data: core::ptr::null_mut(),
        read: None,
        write: None,
        seek: None,
        get_pos: None,
    }
}

struct RustSide {
    s: Box<serializer>,
    live: bool,
    output: bool,
}

struct OracleSide {
    s: Box<OracleSerializer>,
    live: bool,
    output: bool,
}

impl RustSide {
    fn output(path: PathBuf, ext: Option<&str>) -> Self {
        let mut s = Box::new(empty_rust());
        let c_path = c_path(&path);
        // SAFETY: `s` is a writable serializer. `c_path` and `ext` are C strings.
        let ok = unsafe {
            match ext {
                None => file_output_serializer_init(&mut *s, c_path.as_ptr()),
                Some(ext) => {
                    let c_ext = CString::new(ext).unwrap();
                    file_output_serializer_init_safe(&mut *s, c_path.as_ptr(), c_ext.as_ptr())
                }
            }
        };
        assert!(ok, "rust output init {}", path.display());
        Self {
            s,
            live: true,
            output: true,
        }
    }

    fn input(path: PathBuf) -> Self {
        let mut s = Box::new(empty_rust());
        let c_path = c_path(&path);
        // SAFETY: `s` is writable. `c_path` is a C string.
        let ok = unsafe { file_input_serializer_init(&mut *s, c_path.as_ptr()) };
        assert!(ok, "rust input init {}", path.display());
        Self {
            s,
            live: true,
            output: false,
        }
    }

    fn close(&mut self) {
        if self.live {
            // SAFETY: `s` came from a successful init and has not been freed.
            unsafe {
                if self.output {
                    file_output_serializer_free(&mut *self.s);
                } else {
                    file_input_serializer_free(&mut *self.s);
                }
            }
            self.live = false;
        }
    }

    fn view(&self) -> View {
        View {
            data: self.s.data,
            read: self.s.read,
            write: self.s.write,
            seek: self.s.seek,
            get_pos: self.s.get_pos,
        }
    }
}

impl Drop for RustSide {
    fn drop(&mut self) {
        self.close();
    }
}

impl OracleSide {
    fn output(path: PathBuf, ext: Option<&str>) -> Self {
        let mut s = Box::new(empty_oracle());
        let c_path = c_path(&path);
        // SAFETY: `s` is a writable serializer. `c_path` and `ext` are C strings.
        let ok = unsafe {
            match ext {
                None => oracle_file_output_serializer_init(&mut *s, c_path.as_ptr()),
                Some(ext) => {
                    let c_ext = CString::new(ext).unwrap();
                    oracle_file_output_serializer_init_safe(
                        &mut *s,
                        c_path.as_ptr(),
                        c_ext.as_ptr(),
                    )
                }
            }
        };
        assert!(ok, "oracle output init {}", path.display());
        Self {
            s,
            live: true,
            output: true,
        }
    }

    fn input(path: PathBuf) -> Self {
        let mut s = Box::new(empty_oracle());
        let c_path = c_path(&path);
        // SAFETY: `s` is writable. `c_path` is a C string.
        let ok = unsafe { oracle_file_input_serializer_init(&mut *s, c_path.as_ptr()) };
        assert!(ok, "oracle input init {}", path.display());
        Self {
            s,
            live: true,
            output: false,
        }
    }

    fn close(&mut self) {
        if self.live {
            // SAFETY: `s` came from a successful init and has not been freed.
            unsafe {
                if self.output {
                    oracle_file_output_serializer_free(&mut *self.s);
                } else {
                    oracle_file_input_serializer_free(&mut *self.s);
                }
            }
            self.live = false;
        }
    }

    fn view(&self) -> View {
        View {
            data: self.s.data,
            read: self.s.read,
            write: self.s.write,
            seek: self.s.seek,
            get_pos: self.s.get_pos,
        }
    }
}

impl Drop for OracleSide {
    fn drop(&mut self) {
        self.close();
    }
}

fn compare_output(
    scratch: &Scratch,
    name: &str,
    ext: Option<&str>,
    preexisting: Option<&[u8]>,
    ops: &[Op],
) -> Vec<u8> {
    let rust_path = scratch.join(&format!("rust-{name}"));
    let oracle_path = scratch.join(&format!("oracle-{name}"));
    if let Some(bytes) = preexisting {
        std::fs::write(&rust_path, bytes).unwrap();
        std::fs::write(&oracle_path, bytes).unwrap();
    }
    let mut rust = RustSide::output(rust_path.clone(), ext);
    let mut oracle = OracleSide::output(oracle_path.clone(), ext);
    assert!(rust.view().read.is_none());
    assert!(oracle.view().read.is_none());
    assert!(rust.view().write.is_some());
    assert!(oracle.view().write.is_some());
    for op in ops {
        assert_eq!(run(&rust.view(), op), run(&oracle.view(), op), "{op:?}");
    }
    if let Some(ext) = ext {
        assert!(temp_of(&rust_path, ext).exists());
        assert!(temp_of(&oracle_path, ext).exists());
        match preexisting {
            Some(bytes) => {
                assert_eq!(std::fs::read(&rust_path).unwrap(), bytes);
                assert_eq!(std::fs::read(&oracle_path).unwrap(), bytes);
            }
            None => {
                assert!(!rust_path.exists());
                assert!(!oracle_path.exists());
            }
        }
    }
    rust.close();
    oracle.close();
    let rust_bytes = std::fs::read(&rust_path).unwrap();
    let oracle_bytes = std::fs::read(&oracle_path).unwrap();
    assert_eq!(rust_bytes, oracle_bytes, "{name}");
    if let Some(ext) = ext {
        assert!(!temp_of(&rust_path, ext).exists());
        assert!(!temp_of(&oracle_path, ext).exists());
    }
    rust_bytes
}

fn compare_input(scratch: &Scratch, content: &[u8], ops: &[Op]) -> Vec<Outcome> {
    let rust_path = scratch.join("rust-in");
    let oracle_path = scratch.join("oracle-in");
    std::fs::write(&rust_path, content).unwrap();
    std::fs::write(&oracle_path, content).unwrap();
    let rust = RustSide::input(rust_path);
    let oracle = OracleSide::input(oracle_path);
    assert!(rust.view().write.is_none());
    assert!(oracle.view().write.is_none());
    assert!(rust.view().read.is_some());
    assert!(oracle.view().read.is_some());
    let mut got = Vec::new();
    for op in ops {
        let ours = run(&rust.view(), op);
        let theirs = run(&oracle.view(), op);
        if cfg!(windows) && matches!(op, Op::Seek(..)) && ours.ret == -1 {
            // MSVC failed-seek state, see the header.
            assert_eq!(theirs.ret, -1, "{op:?}");
            got.push(ours);
            break;
        }
        assert_eq!(ours, theirs, "{op:?}");
        got.push(ours);
    }
    got
}

#[test]
fn output_input_roundtrip_matches_oracle() {
    let scratch = Scratch::new();
    let expected = [
        0xAAu8, 0x01, 0x02, 0x03, 0x04, 0x12, 0x34, b'h', b'e', b'l', b'l', b'o',
    ];
    let got = compare_output(
        &scratch,
        "round",
        None,
        None,
        &[Op::Write(expected.to_vec()), Op::Write(Vec::new()), Op::Pos],
    );
    assert_eq!(got, expected);
    let back = compare_input(
        &scratch,
        &expected,
        &[Op::Read(expected.len()), Op::Read(1), Op::Pos],
    );
    assert_eq!(back[0].bytes, expected);
    assert_eq!(back[1].ret, 0);
    assert_eq!(back[2].ret, expected.len() as i64);
}

#[test]
fn output_seek_overwrite_matches_oracle() {
    let scratch = Scratch::new();
    let got = compare_output(
        &scratch,
        "seek",
        None,
        None,
        &[
            Op::Pos,
            Op::Write(b"0123456789".to_vec()),
            Op::Pos,
            Op::Seek(4, 0),
            Op::Pos,
            Op::Write(b"X".to_vec()),
            Op::Seek(2, 1),
            Op::Seek(-1, 2),
            Op::Write(b"Z".to_vec()),
            Op::Pos,
        ],
    );
    assert_eq!(got, b"0123X5678Z");
}

#[test]
fn input_seek_matches_oracle() {
    let scratch = Scratch::new();
    let back = compare_input(
        &scratch,
        b"ABCDEFGHIJ",
        &[
            Op::Pos,
            Op::Read(3),
            Op::Seek(6, 0),
            Op::Read(2),
            Op::Seek(-4, 1),
            Op::Read(1),
            Op::Seek(-2, 2),
            Op::Read(2),
            Op::Seek(0, 2),
            Op::Read(1),
            Op::Seek(8, 0),
            Op::Read(4),
            Op::Seek(-1, 0),
            Op::Pos,
        ],
    );
    assert_eq!(back[1].bytes, b"ABC");
    assert_eq!(back[3].bytes, b"GH");
    assert_eq!(back[5].bytes, b"E");
    assert_eq!(back[7].bytes, b"IJ");
    assert_eq!(back[9].ret, 0);
    assert_eq!(back[11].ret, 2);
    assert_eq!(back[11].bytes, b"IJ");
    assert_eq!(back[12].ret, -1);
    // On Windows the comparison stops at the failed seek; see the header.
    if !cfg!(windows) {
        assert_eq!(back[13].ret, back[12].pos);
    }
}

#[test]
fn unknown_seek_code_matches_oracle() {
    let scratch = Scratch::new();
    let got = compare_output(
        &scratch,
        "unk",
        None,
        None,
        &[
            Op::Write(b"abcdefghij".to_vec()),
            Op::Seek(3, 99),
            Op::Write(b"Q".to_vec()),
        ],
    );
    assert_eq!(got, b"abcQefghij");
}

#[test]
fn safe_ext_and_replace_match_oracle() {
    let scratch = Scratch::new();
    let plain = compare_output(
        &scratch,
        "ext",
        Some("tmp"),
        None,
        &[Op::Write(b"payload".to_vec())],
    );
    assert_eq!(plain, b"payload");
    let dotted = compare_output(
        &scratch,
        "dot",
        Some(".tmp"),
        None,
        &[Op::Write(b"payload".to_vec())],
    );
    assert_eq!(dotted, b"payload");
    let replaced = compare_output(
        &scratch,
        "repl",
        Some("tmp"),
        Some(b"old"),
        &[Op::Write(b"newdata".to_vec())],
    );
    assert_eq!(replaced, b"newdata");
    let seek = compare_output(
        &scratch,
        "sseek",
        Some("tmp"),
        None,
        &[
            Op::Write(b"abcdef".to_vec()),
            Op::Pos,
            Op::Seek(1, 0),
            Op::Write(b"Z".to_vec()),
        ],
    );
    assert_eq!(seek, b"aZcdef");
}

#[test]
fn missing_and_bad_ext_match_oracle() {
    let scratch = Scratch::new();
    let missing = scratch.join("missing.bin");
    let mut rust = Box::new(empty_rust());
    let mut oracle = Box::new(empty_oracle());
    rust.data = core::ptr::without_provenance_mut(1);
    oracle.data = core::ptr::without_provenance_mut(1);
    let c_missing = c_path(&missing);
    // SAFETY: both serializers are writable. The path is a C string. Init
    // fails, so nothing is freed.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_input_serializer_init(&mut *rust, c_missing.as_ptr()),
            oracle_file_input_serializer_init(&mut *oracle, c_missing.as_ptr()),
        )
    };
    assert!(!rust_ok && !oracle_ok);
    assert!(rust.data.is_null() && oracle.data.is_null());

    let bad = scratch.join("no_such_subdir/x.bin");
    let c_bad = c_path(&bad);
    let mut rust = Box::new(empty_rust());
    let mut oracle = Box::new(empty_oracle());
    rust.data = core::ptr::without_provenance_mut(1);
    oracle.data = core::ptr::without_provenance_mut(1);
    // SAFETY: init fails without writing `s` or creating a file.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_output_serializer_init(&mut *rust, c_bad.as_ptr()),
            oracle_file_output_serializer_init(&mut *oracle, c_bad.as_ptr()),
        )
    };
    assert!(!rust_ok && !oracle_ok);
    assert_eq!(rust.data, core::ptr::without_provenance_mut(1));
    assert_eq!(oracle.data, core::ptr::without_provenance_mut(1));

    let c_ext = CString::new("tmp").unwrap();
    // SAFETY: same path, safe init. The parent directory does not exist.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_output_serializer_init_safe(&mut *rust, c_bad.as_ptr(), c_ext.as_ptr()),
            oracle_file_output_serializer_init_safe(&mut *oracle, c_bad.as_ptr(), c_ext.as_ptr()),
        )
    };
    assert!(!rust_ok && !oracle_ok);
    assert_eq!(rust.data, core::ptr::without_provenance_mut(1));
    assert_eq!(oracle.data, core::ptr::without_provenance_mut(1));
    assert!(!bad.exists());
    assert!(!temp_of(&bad, "tmp").exists());

    let safe = scratch.join("safe.bin");
    let c_safe = c_path(&safe);
    let empty_ext = CString::new("").unwrap();
    // SAFETY: an empty extension is rejected before any file is created.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_output_serializer_init_safe(&mut *rust, c_safe.as_ptr(), empty_ext.as_ptr()),
            oracle_file_output_serializer_init_safe(
                &mut *oracle,
                c_safe.as_ptr(),
                empty_ext.as_ptr(),
            ),
        )
    };
    assert!(!rust_ok && !oracle_ok);
    assert!(!safe.exists());

    // SAFETY: a null extension is rejected before any file is created.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_output_serializer_init_safe(&mut *rust, c_safe.as_ptr(), core::ptr::null()),
            oracle_file_output_serializer_init_safe(
                &mut *oracle,
                c_safe.as_ptr(),
                core::ptr::null(),
            ),
        )
    };
    assert!(!rust_ok && !oracle_ok);
    assert!(!safe.exists());
    assert!(!temp_of(&safe, "tmp").exists());
}

#[test]
fn null_path_plain_init_matches_oracle() {
    let mut rust = empty_rust();
    let mut oracle = empty_oracle();
    let sentinel = core::ptr::without_provenance_mut(1);
    rust.data = sentinel;
    oracle.data = sentinel;
    // SAFETY: a null path makes `os_fopen` fail. Output init does not write `s`.
    let (rust_out, oracle_out) = unsafe {
        (
            file_output_serializer_init(&mut rust, core::ptr::null()),
            oracle_file_output_serializer_init(&mut oracle, core::ptr::null()),
        )
    };
    assert!(!rust_out && !oracle_out);
    assert_eq!(rust.data, sentinel);
    assert_eq!(oracle.data, sentinel);

    // SAFETY: input init stores the null `FILE *` and returns false.
    let (rust_in, oracle_in) = unsafe {
        (
            file_input_serializer_init(&mut rust, core::ptr::null()),
            oracle_file_input_serializer_init(&mut oracle, core::ptr::null()),
        )
    };
    assert!(!rust_in && !oracle_in);
    assert!(rust.data.is_null() && oracle.data.is_null());
}

/// Intentional difference. The oracle is not called: C would create a
/// relative temp file in the process cwd.
#[test]
fn null_path_safe_init_returns_false() {
    let mut rust = empty_rust();
    rust.data = core::ptr::without_provenance_mut(1);
    let ext = CString::new("tmp").unwrap();
    // SAFETY: null path. The shim returns before creating a file, and does
    // not write `s`.
    let ok =
        unsafe { file_output_serializer_init_safe(&mut rust, core::ptr::null(), ext.as_ptr()) };
    assert!(!ok);
    assert_eq!(rust.data, core::ptr::without_provenance_mut(1));
}

/// C crashes on a null serializer. The shim returns.
#[test]
fn free_null_serializer_returns() {
    // SAFETY: both frees return immediately on a null serializer.
    unsafe {
        file_input_serializer_free(core::ptr::null_mut());
        file_output_serializer_free(core::ptr::null_mut());
    }
}

fn output_op() -> impl Strategy<Value = Op> {
    prop_oneof![
        proptest::collection::vec(any::<u8>(), 0..32).prop_map(Op::Write),
        (-8i64..24, prop::sample::select(vec![0, 1, 2, 99, -1]))
            .prop_map(|(offset, kind)| Op::Seek(offset, kind)),
        Just(Op::Pos),
    ]
}

fn input_op() -> impl Strategy<Value = Op> {
    prop_oneof![
        (0usize..24).prop_map(Op::Read),
        (-8i64..24, prop::sample::select(vec![0, 1, 2, 99, -1]))
            .prop_map(|(offset, kind)| Op::Seek(offset, kind)),
        Just(Op::Pos),
    ]
}

proptest! {
    #[test]
    fn plain_output_matches_oracle(ops in proptest::collection::vec(output_op(), 0..16)) {
        let scratch = Scratch::new();
        compare_output(&scratch, "prop", None, None, &ops);
    }

    #[test]
    fn safe_output_matches_oracle(
        ops in proptest::collection::vec(output_op(), 0..16),
        ext in prop::sample::select(vec!["tmp", ".tmp", "bak"]),
    ) {
        let scratch = Scratch::new();
        compare_output(&scratch, "sprop", Some(ext), None, &ops);
    }

    #[test]
    fn input_matches_oracle(
        content in proptest::collection::vec(any::<u8>(), 0..80),
        ops in proptest::collection::vec(input_op(), 0..16),
    ) {
        let scratch = Scratch::new();
        compare_input(&scratch, &content, &ops);
    }
}

/// A filename that is not UTF-8. Where the filesystem rejects the byte
/// (APFS) both inits fail; where it accepts it (Linux ext4) both create the
/// same contents. On Windows libobs converts with U+FFFD substitution
/// (utf8_to_wchar, flags 0) and creates the substituted name, so both sides
/// must too.
#[test]
fn non_utf8_filename_matches_oracle() {
    let scratch = Scratch::new();
    let mut rust_name = path_bytes(&scratch.join("rust-raw"));
    let mut oracle_name = path_bytes(&scratch.join("oracle-raw"));
    rust_name.push(0xFF);
    oracle_name.push(0xFF);
    let c_rust = CString::new(rust_name.clone()).unwrap();
    let c_oracle = CString::new(oracle_name.clone()).unwrap();
    let mut rust_s = Box::new(empty_rust());
    let mut oracle_s = Box::new(empty_oracle());
    // SAFETY: both serializers are writable. The paths are C strings with no
    // interior NUL. A failed output init does not write `s`.
    let (rust_ok, oracle_ok) = unsafe {
        (
            file_output_serializer_init(&mut *rust_s, c_rust.as_ptr()),
            oracle_file_output_serializer_init(&mut *oracle_s, c_oracle.as_ptr()),
        )
    };
    assert_eq!(rust_ok, oracle_ok);
    if !rust_ok {
        assert!(rust_s.data.is_null());
        assert!(oracle_s.data.is_null());
        // Nothing was created: the fresh scratch dir is still empty.
        assert!(std::fs::read_dir(&scratch.0).unwrap().next().is_none());
        return;
    }
    let rust_path = path_from_bytes(&rust_name);
    let oracle_path = path_from_bytes(&oracle_name);
    let ops = [
        Op::Write(b"hi".to_vec()),
        Op::Seek(1, 0),
        Op::Write(b"Z".to_vec()),
    ];
    let mut rust = RustSide {
        s: rust_s,
        live: true,
        output: true,
    };
    let mut oracle = OracleSide {
        s: oracle_s,
        live: true,
        output: true,
    };
    for op in &ops {
        assert_eq!(run(&rust.view(), op), run(&oracle.view(), op));
    }
    rust.close();
    oracle.close();
    assert_eq!(
        std::fs::read(rust_path).unwrap(),
        std::fs::read(oracle_path).unwrap()
    );
}
