//! Tier 1: the obs-nvenc loader's safe core against a fake environment and a
//! fake `DynLoader` (obs-rust/obs-studio#520). No GPU needed; every host's
//! candidate list is checked on every OS. There is no C test to mirror
//! (plugins/obs-nvenc has none), so these pin the issue's design.

use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::ffi::{OsString, c_void};

use obs_nvenc::{
    Candidate, CandidateSource, CudaDriver, CudaVersion, DynLib, DynLoader, Env, Host, Library,
    LoadError, OPTIONAL_CUDA_SYMBOLS, Symbol, candidates_for, default_candidates,
    parse_glibc_requirement, required_cuda_symbols,
};

#[derive(Default)]
struct FakeEnv(HashMap<&'static str, &'static str>);

impl Env for FakeEnv {
    fn var(&self, key: &str) -> Option<OsString> {
        self.0.get(key).map(OsString::from)
    }
}

#[derive(Clone)]
struct FakeLib {
    symbols: HashSet<String>,
    version: i32,
}

impl FakeLib {
    fn full(host: Host) -> Self {
        let mut symbols: HashSet<String> = required_cuda_symbols(host)
            .into_iter()
            .map(str::to_owned)
            .collect();
        symbols.extend(OPTIONAL_CUDA_SYMBOLS.iter().map(|s| (*s).to_owned()));
        FakeLib {
            symbols,
            version: 12040,
        }
    }

    fn without(mut self, name: &str) -> Self {
        self.symbols.remove(name);
        self
    }
}

impl DynLib for FakeLib {
    fn symbol(&self, name: &str) -> Option<Symbol> {
        // A recognisable fake address; it is never called.
        self.symbols
            .contains(name)
            .then(|| Symbol::from_ptr(std::ptr::dangling_mut::<c_void>()))
            .flatten()
    }

    fn call_driver_get_version(&self, _sym: Symbol) -> (i32, i32) {
        (0, self.version)
    }
}

/// Maps a candidate name to an outcome and records the open order.
#[derive(Default)]
struct FakeLoader {
    outcomes: HashMap<OsString, Result<FakeLib, String>>,
    opened: RefCell<Vec<OsString>>,
}

impl FakeLoader {
    fn with(mut self, name: &str, outcome: Result<FakeLib, &str>) -> Self {
        self.outcomes
            .insert(name.into(), outcome.map_err(str::to_owned));
        self
    }
}

impl DynLoader for FakeLoader {
    fn open(&self, c: &Candidate) -> Result<Box<dyn DynLib>, String> {
        self.opened.borrow_mut().push(c.name.clone());
        match self.outcomes.get(&c.name) {
            Some(Ok(lib)) => Ok(Box::new(lib.clone())),
            Some(Err(e)) => Err(e.clone()),
            None => Err(format!(
                "{}: cannot open shared object file: No such file or directory",
                c.name.to_string_lossy()
            )),
        }
    }
}

fn names(cands: &[Candidate]) -> Vec<String> {
    cands
        .iter()
        .map(|c| c.name.to_string_lossy().into_owned())
        .collect()
}

fn cand(name: &str, source: CandidateSource) -> Candidate {
    Candidate {
        name: name.into(),
        source,
    }
}

const LINUX: Host = Host::Linux;
const WIN64: Host = Host::Windows { arm64: false };
const WIN_ARM64: Host = Host::Windows { arm64: true };

// ---- candidate order ----

#[test]
fn linux_cuda_order_matches_design() {
    let c = default_candidates(Library::CudaDriver, LINUX);
    assert_eq!(
        names(&c),
        [
            "libcuda.so.1",
            "libcuda.so",
            "/usr/lib/x86_64-linux-gnu/libcuda.so.1",
            "/usr/lib/aarch64-linux-gnu/libcuda.so.1",
            "/usr/lib64/libcuda.so.1",
            "/usr/lib/nvidia/libcuda.so.1",
            "/run/opengl-driver/lib/libcuda.so.1",
            "/usr/lib/wsl/lib/libcuda.so.1",
        ]
    );
    assert_eq!(c[0].source, CandidateSource::SonameDefault);
    assert_eq!(c[1].source, CandidateSource::SonameUnversioned);
    assert!(
        c[2..7]
            .iter()
            .all(|c| c.source == CandidateSource::DistroPath)
    );
    assert_eq!(c[7].source, CandidateSource::WslPath);
}

#[test]
fn linux_nvenc_and_nvml_follow_the_same_pattern() {
    let e = names(&default_candidates(Library::NvEncode, LINUX));
    assert_eq!(e[0], "libnvidia-encode.so.1");
    assert_eq!(e[1], "libnvidia-encode.so");
    assert_eq!(e.last().unwrap(), "/usr/lib/wsl/lib/libnvidia-encode.so.1");
    assert_eq!(e.len(), 8);
    let m = names(&default_candidates(Library::Nvml, LINUX));
    assert_eq!(m[0], "libnvidia-ml.so.1");
    assert_eq!(m.len(), 8);
}

#[test]
fn windows_lists_are_bare_system32_dll_names() {
    assert_eq!(
        names(&default_candidates(Library::CudaDriver, WIN64)),
        ["nvcuda.dll"]
    );
    assert_eq!(
        names(&default_candidates(Library::NvEncode, WIN64)),
        ["nvEncodeAPI64.dll", "nvEncodeAPI.dll"]
    );
    // nvenc-load.c opens nvEncodeAPI.dll on _M_ARM64.
    assert_eq!(
        names(&default_candidates(Library::NvEncode, WIN_ARM64)),
        ["nvEncodeAPI.dll", "nvEncodeAPI64.dll"]
    );
    assert_eq!(
        names(&default_candidates(Library::Nvml, WIN64)),
        ["nvml.dll"]
    );
    for lib in [Library::CudaDriver, Library::NvEncode, Library::Nvml] {
        for c in default_candidates(lib, WIN64) {
            assert_eq!(c.source, CandidateSource::SonameDefault);
            assert!(!c.name.to_string_lossy().contains(['/', '\\']));
        }
    }
}

#[test]
fn macos_lists_are_empty() {
    for lib in [Library::CudaDriver, Library::NvEncode, Library::Nvml] {
        assert!(default_candidates(lib, Host::MacOs).is_empty());
    }
}

#[test]
fn env_override_comes_first() {
    let env = FakeEnv(HashMap::from([
        ("OBS_CUDA_LIBRARY", "/opt/nv/libcuda.so.1"),
        ("OBS_NVENC_LIBRARY", "/opt/nv/libnvidia-encode.so.1"),
    ]));
    let c = candidates_for(Library::CudaDriver, LINUX, &env);
    assert_eq!(
        c[0],
        cand("/opt/nv/libcuda.so.1", CandidateSource::EnvOverride)
    );
    assert_eq!(c[1..], default_candidates(Library::CudaDriver, LINUX)[..]);
    let e = candidates_for(Library::NvEncode, WIN64, &env);
    assert_eq!(names(&e)[0], "/opt/nv/libnvidia-encode.so.1");
    // macOS still honours an explicit override.
    let m = candidates_for(Library::CudaDriver, Host::MacOs, &env);
    assert_eq!(names(&m), ["/opt/nv/libcuda.so.1"]);
}

#[test]
fn no_or_empty_override_gives_defaults() {
    for env in [
        FakeEnv::default(),
        FakeEnv(HashMap::from([("OBS_CUDA_LIBRARY", "")])),
    ] {
        for host in [LINUX, WIN64, Host::MacOs] {
            for lib in [Library::CudaDriver, Library::NvEncode, Library::Nvml] {
                assert_eq!(
                    candidates_for(lib, host, &env),
                    default_candidates(lib, host)
                );
            }
        }
    }
}

// ---- symbol table ----

#[test]
fn required_symbols_mirror_cuda_functions_table() {
    let common = [
        "cuInit",
        "cuDeviceGetCount",
        "cuDeviceGet",
        "cuDeviceGetAttribute",
        "cuCtxCreate_v2",
        "cuCtxDestroy_v2",
        "cuCtxPushCurrent_v2",
        "cuCtxPopCurrent_v2",
        "cuArray3DCreate_v2",
        "cuArrayDestroy",
        "cuMemcpy2D_v2",
        "cuGetErrorName",
        "cuGetErrorString",
        "cuMemHostRegister_v2",
        "cuMemHostUnregister",
    ];
    let gl = [
        "cuGLGetDevices_v2",
        "cuGraphicsGLRegisterImage",
        "cuGraphicsUnregisterResource",
        "cuGraphicsMapResources",
        "cuGraphicsUnmapResources",
        "cuGraphicsSubResourceGetMappedArray",
    ];
    assert_eq!(required_cuda_symbols(WIN64), common);
    let linux: Vec<_> = common.iter().chain(gl.iter()).copied().collect();
    assert_eq!(required_cuda_symbols(LINUX), linux);
}

// ---- load_from ----

#[test]
fn first_fails_second_succeeds() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let dl = FakeLoader::default().with("libcuda.so", Ok(FakeLib::full(LINUX)));
    let drv = CudaDriver::load_from_host(&cands, &dl, LINUX).expect("loads");
    assert_eq!(
        drv.candidate(),
        &cand("libcuda.so", CandidateSource::SonameUnversioned)
    );
    assert_eq!(*dl.opened.borrow(), ["libcuda.so.1", "libcuda.so"]);
    assert!(drv.symbol("cuCtxCreate_v2").is_some());
    assert!(drv.symbol("cuDriverGetVersion").is_some());
    assert_eq!(drv.driver_version().unwrap(), CudaVersion(12040));
}

#[test]
fn all_fail_lists_every_dlerror() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let dl = FakeLoader::default();
    let err = CudaDriver::load_from_host(&cands, &dl, LINUX).unwrap_err();
    let LoadError::NotFound { library, tried } = &err else {
        panic!("expected NotFound, got {err:?}");
    };
    assert_eq!(*library, Library::CudaDriver);
    assert_eq!(tried.len(), cands.len());
    for ((c, msg), want) in tried.iter().zip(&cands) {
        assert_eq!(c, want);
        assert!(msg.contains("No such file"), "{msg}");
    }
    let text = err.to_string();
    assert!(
        text.starts_with("[NVENC] Test process failed: cuda_lib"),
        "{text}"
    );
    assert!(text.contains("/usr/lib/wsl/lib/libcuda.so.1"), "{text}");
}

#[test]
fn empty_candidate_list_is_not_found() {
    let err = CudaDriver::load_from_host(&[], &FakeLoader::default(), Host::MacOs).unwrap_err();
    assert_eq!(
        err,
        LoadError::NotFound {
            library: Library::CudaDriver,
            tried: vec![]
        }
    );
}

#[test]
fn glibc_dlerror_gives_glibc_mismatch() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let msg = "/lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.44' not found \
               (required by /usr/lib/x86_64-linux-gnu/libcuda.so.1)";
    let dl = FakeLoader::default().with("libcuda.so.1", Err(msg));
    let err = CudaDriver::load_from_host(&cands, &dl, LINUX).unwrap_err();
    assert_eq!(
        err,
        LoadError::GlibcMismatch {
            candidate: cands[0].clone(),
            required: "GLIBC_2.44".into()
        }
    );
    assert!(err.to_string().contains("GLIBC_2.44"), "{err}");
}

#[test]
fn a_later_success_beats_an_earlier_glibc_mismatch() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let dl = FakeLoader::default()
        .with("libcuda.so.1", Err("version `GLIBC_2.44' not found"))
        .with("/usr/lib64/libcuda.so.1", Ok(FakeLib::full(LINUX)));
    let drv = CudaDriver::load_from_host(&cands, &dl, LINUX).unwrap();
    assert_eq!(drv.candidate().name, "/usr/lib64/libcuda.so.1");
}

#[test]
fn parse_glibc_requirement_cases() {
    assert_eq!(
        parse_glibc_requirement("version `GLIBC_2.44' not found (required by x)").as_deref(),
        Some("GLIBC_2.44")
    );
    assert_eq!(
        parse_glibc_requirement("version 'GLIBC_2.38' not found").as_deref(),
        Some("GLIBC_2.38")
    );
    assert_eq!(
        parse_glibc_requirement("version `GLIBC_PRIVATE' not found").as_deref(),
        Some("GLIBC_PRIVATE")
    );
    assert_eq!(
        parse_glibc_requirement("libcuda.so.1: cannot open shared object file"),
        None
    );
    assert_eq!(parse_glibc_requirement("version `GLIBC_2.44"), None);
    assert_eq!(parse_glibc_requirement(""), None);
}

#[test]
fn missing_required_symbol() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let dl = FakeLoader::default().with(
        "libcuda.so.1",
        Ok(FakeLib::full(LINUX).without("cuCtxCreate_v2")),
    );
    let err = CudaDriver::load_from_host(&cands, &dl, LINUX).unwrap_err();
    assert_eq!(
        err,
        LoadError::MissingSymbol {
            library: Library::CudaDriver,
            symbol: "cuCtxCreate_v2"
        }
    );
    assert!(err.to_string().contains("cuCtxCreate_v2"), "{err}");
}

#[test]
fn gl_symbols_not_required_on_windows() {
    let cands = default_candidates(Library::CudaDriver, WIN64);
    let dl = FakeLoader::default().with("nvcuda.dll", Ok(FakeLib::full(WIN64)));
    assert!(CudaDriver::load_from_host(&cands, &dl, WIN64).is_ok());
    // The Windows table lacks the GL entries, which Linux requires.
    let dl = FakeLoader::default().with("libcuda.so.1", Ok(FakeLib::full(WIN64)));
    let err =
        CudaDriver::load_from_host(&default_candidates(Library::CudaDriver, LINUX), &dl, LINUX)
            .unwrap_err();
    assert!(matches!(
        err,
        LoadError::MissingSymbol {
            symbol: "cuGLGetDevices_v2",
            ..
        }
    ));
}

#[test]
fn missing_optional_symbol_leaves_none() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let dl = FakeLoader::default().with(
        "libcuda.so.1",
        Ok(FakeLib::full(LINUX).without("cuDriverGetVersion")),
    );
    let drv = CudaDriver::load_from_host(&cands, &dl, LINUX).expect("optional");
    assert!(drv.symbol("cuDriverGetVersion").is_none());
    assert!(drv.driver_version().is_err());
}

#[test]
fn driver_too_old() {
    let cands = default_candidates(Library::CudaDriver, LINUX);
    let mut lib = FakeLib::full(LINUX);
    lib.version = 11080;
    let dl = FakeLoader::default().with("libcuda.so.1", Ok(lib));
    let drv = CudaDriver::load_from_host(&cands, &dl, LINUX).unwrap();
    let err = drv.require_version(CudaVersion(12000)).unwrap_err();
    assert_eq!(
        err,
        LoadError::DriverTooOld {
            found: CudaVersion(11080),
            need: CudaVersion(12000)
        }
    );
    assert!(err.to_string().contains("11.8"), "{err}");
    assert!(err.to_string().contains("12.0"), "{err}");
    assert_eq!(
        drv.require_version(CudaVersion(11080)).unwrap(),
        CudaVersion(11080)
    );
}

#[test]
fn cuda_version_parts() {
    assert_eq!(CudaVersion(12040).major(), 12);
    assert_eq!(CudaVersion(12040).minor(), 4);
    assert_eq!(CudaVersion(11080).to_string(), "11.8");
}

/// Opt-in, for maintainers with NVIDIA hardware:
/// `soldr cargo test -p obs-nvenc -- --ignored real_gpu_loads --nocapture`.
#[test]
#[ignore = "needs an NVIDIA driver"]
fn real_gpu_loads() {
    let drv = CudaDriver::load().unwrap_or_else(|e| panic!("{e}"));
    println!("loaded {:?}", drv.candidate());
    println!("driver version {}", drv.driver_version().expect("version"));
}
