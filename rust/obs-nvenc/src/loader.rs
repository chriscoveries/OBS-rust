//! Walks the candidates and resolves the CUDA driver function table.

use std::collections::HashMap;
use std::ffi::c_void;
use std::fmt;
use std::ptr::NonNull;

use crate::{
    Candidate, CudaError, Host, Library, LoadError, SystemEnv, SystemLoader, candidates,
    parse_glibc_requirement,
};

/// A resolved symbol address. Only the platform loader turns it back into a
/// callable function.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Symbol(NonNull<c_void>);

impl Symbol {
    /// Wraps a non-null address.
    pub fn from_ptr(ptr: *mut c_void) -> Option<Symbol> {
        NonNull::new(ptr).map(Symbol)
    }

    /// The raw address.
    pub fn as_ptr(self) -> *mut c_void {
        self.0.as_ptr()
    }
}

/// An opened library.
pub trait DynLib {
    /// Resolves `name`, or `None` if the library does not export it.
    fn symbol(&self, name: &str) -> Option<Symbol>;
    /// Calls `cuDriverGetVersion` through `sym`; returns `(CUresult, version)`.
    fn call_driver_get_version(&self, sym: Symbol) -> (i32, i32);
}

/// Opens a candidate; the error string is the loader's message (`dlerror`).
pub trait DynLoader {
    fn open(&self, c: &Candidate) -> Result<Box<dyn DynLib>, String>;
}

/// A CUDA driver API version (`cuDriverGetVersion`: `1000 * major + 10 * minor`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct CudaVersion(pub i32);

impl CudaVersion {
    pub fn major(self) -> i32 {
        self.0 / 1000
    }
    pub fn minor(self) -> i32 {
        (self.0 % 1000) / 10
    }
}

impl fmt::Display for CudaVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}", self.major(), self.minor())
    }
}

/// Symbols resolved when present but not required (not in `cuda_functions[]`).
pub const OPTIONAL_CUDA_SYMBOLS: &[&str] = &["cuDriverGetVersion"];

/// The names in `cuda_functions[]` (`plugins/obs-nvenc/cuda-load.c`), in
/// order, for `host`; the GL interop entries are absent on Windows.
pub fn required_cuda_symbols(host: Host) -> Vec<&'static str> {
    const COMMON: &[&str] = &[
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
    // `#ifndef _WIN32` in cuda-load.c.
    const GL_INTEROP: &[&str] = &[
        "cuGLGetDevices_v2",
        "cuGraphicsGLRegisterImage",
        "cuGraphicsUnregisterResource",
        "cuGraphicsMapResources",
        "cuGraphicsUnmapResources",
        "cuGraphicsSubResourceGetMappedArray",
    ];
    let mut out = COMMON.to_vec();
    if !matches!(host, Host::Windows { .. }) {
        out.extend_from_slice(GL_INTEROP);
    }
    out
}

/// Opens the first candidate that loads. When none does, a `GLIBC_x.y`
/// mismatch is reported in preference to the plain `NotFound` list.
pub fn open_library(
    library: Library,
    cands: &[Candidate],
    dl: &dyn DynLoader,
) -> Result<(Candidate, Box<dyn DynLib>), LoadError> {
    let mut tried = Vec::with_capacity(cands.len());
    for c in cands {
        match dl.open(c) {
            Ok(lib) => return Ok((c.clone(), lib)),
            Err(why) => tried.push((c.clone(), why)),
        }
    }
    if let Some((candidate, required)) = tried
        .iter()
        .find_map(|(c, why)| parse_glibc_requirement(why).map(|r| (c.clone(), r)))
    {
        return Err(LoadError::GlibcMismatch {
            candidate,
            required,
        });
    }
    Err(LoadError::NotFound { library, tried })
}

/// The loaded CUDA driver and its resolved function table.
pub struct CudaDriver {
    candidate: Candidate,
    symbols: HashMap<&'static str, Symbol>,
    lib: Box<dyn DynLib>,
}

impl fmt::Debug for CudaDriver {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("CudaDriver")
            .field("candidate", &self.candidate)
            .field("symbols", &self.symbols.len())
            .finish()
    }
}

impl CudaDriver {
    /// Loads the driver from the current host's candidates.
    pub fn load() -> Result<Self, LoadError> {
        Self::load_from(&candidates(Library::CudaDriver, &SystemEnv), &SystemLoader)
    }

    /// Loads the driver from `cands` through `dl`, for the current host's
    /// symbol table.
    pub fn load_from(cands: &[Candidate], dl: &dyn DynLoader) -> Result<Self, LoadError> {
        Self::load_from_host(cands, dl, Host::current())
    }

    /// [`CudaDriver::load_from`] with an explicit host symbol table.
    pub fn load_from_host(
        cands: &[Candidate],
        dl: &dyn DynLoader,
        host: Host,
    ) -> Result<Self, LoadError> {
        let (candidate, lib) = open_library(Library::CudaDriver, cands, dl)?;
        let mut symbols = HashMap::new();
        for name in required_cuda_symbols(host) {
            let sym = lib.symbol(name).ok_or(LoadError::MissingSymbol {
                library: Library::CudaDriver,
                symbol: name,
            })?;
            symbols.insert(name, sym);
        }
        for &name in OPTIONAL_CUDA_SYMBOLS {
            if let Some(sym) = lib.symbol(name) {
                symbols.insert(name, sym);
            }
        }
        Ok(CudaDriver {
            candidate,
            symbols,
            lib,
        })
    }

    /// The candidate that loaded.
    pub fn candidate(&self) -> &Candidate {
        &self.candidate
    }

    /// The resolved address of `name` (required or optional symbol).
    pub fn symbol(&self, name: &str) -> Option<Symbol> {
        self.symbols.get(name).copied()
    }

    /// `cuDriverGetVersion`.
    pub fn driver_version(&self) -> Result<CudaVersion, CudaError> {
        const NAME: &str = "cuDriverGetVersion";
        let sym = self
            .symbol(NAME)
            .ok_or(CudaError::SymbolUnavailable(NAME))?;
        match self.lib.call_driver_get_version(sym) {
            (0, version) => Ok(CudaVersion(version)),
            (result, _) => Err(CudaError::Call {
                function: NAME,
                result,
            }),
        }
    }

    /// Fails with [`LoadError::DriverTooOld`] when the driver is older than `need`.
    pub fn require_version(&self, need: CudaVersion) -> Result<CudaVersion, LoadError> {
        let found = self
            .driver_version()
            .map_err(|_| LoadError::MissingSymbol {
                library: Library::CudaDriver,
                symbol: "cuDriverGetVersion",
            })?;
        if found < need {
            return Err(LoadError::DriverTooOld { found, need });
        }
        Ok(found)
    }
}
