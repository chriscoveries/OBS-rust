//! Typed load errors. `Display` keeps the C plugin's log prefix
//! (`[NVENC] Test process failed: cuda_lib`) so log scrapers see the same
//! text, followed by what was tried and why.

use std::fmt;

use crate::{Candidate, CudaVersion, Library};

/// Why a driver library could not be loaded.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum LoadError {
    /// Every candidate failed to open; each with its loader error.
    NotFound {
        library: Library,
        tried: Vec<(Candidate, String)>,
    },
    /// A candidate exists but needs a newer glibc than the process has
    /// (`version 'GLIBC_2.44' not found`).
    GlibcMismatch {
        candidate: Candidate,
        required: String,
    },
    /// A required symbol is missing from the opened library.
    MissingSymbol {
        library: Library,
        symbol: &'static str,
    },
    /// The driver is older than required.
    DriverTooOld {
        found: CudaVersion,
        need: CudaVersion,
    },
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LoadError::NotFound { library, tried } => {
                write!(f, "[NVENC] Test process failed: {}", library.c_error_tag())?;
                if tried.is_empty() {
                    return write!(f, " (no candidates on this platform)");
                }
                for (c, why) in tried {
                    write!(
                        f,
                        "\n  tried {} ({:?}): {why}",
                        c.name.to_string_lossy(),
                        c.source
                    )?;
                }
                Ok(())
            }
            LoadError::GlibcMismatch {
                candidate,
                required,
            } => write!(
                f,
                "[NVENC] {} needs {required}, which this process's glibc does not provide",
                candidate.name.to_string_lossy()
            ),
            LoadError::MissingSymbol { library, symbol } => write!(
                f,
                "[obs-nvenc] Could not load function: {symbol} ({})",
                library.c_error_tag()
            ),
            LoadError::DriverTooOld { found, need } => write!(
                f,
                "[NVENC] CUDA driver {found} is too old; {need} or newer is required"
            ),
        }
    }
}

impl std::error::Error for LoadError {}

/// A CUDA call failed.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum CudaError {
    /// The driver returned a non-zero `CUresult`.
    Call { function: &'static str, result: i32 },
    /// The optional symbol is not present in this driver.
    SymbolUnavailable(&'static str),
}

impl fmt::Display for CudaError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CudaError::Call { function, result } => {
                write!(f, "CUDA call \"{function}\" failed with {result}")
            }
            CudaError::SymbolUnavailable(s) => write!(f, "CUDA function {s} is not available"),
        }
    }
}

impl std::error::Error for CudaError {}

/// Extracts `GLIBC_x.y` from a dynamic loader message such as
/// ``/usr/lib/libcuda.so.1: version `GLIBC_2.44' not found (required by ...)``.
pub fn parse_glibc_requirement(dlerror: &str) -> Option<String> {
    // glibc quotes the version as `GLIBC_x.y' (older) or 'GLIBC_x.y'.
    let start = dlerror.find("version ")? + "version ".len();
    let rest = dlerror[start..].strip_prefix(['`', '\''])?;
    let end = rest.find('\'')?;
    let version = &rest[..end];
    (version.starts_with("GLIBC_") && rest[end..].starts_with("' not found"))
        .then(|| version.to_owned())
}
