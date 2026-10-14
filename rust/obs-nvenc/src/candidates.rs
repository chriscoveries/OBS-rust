//! Which file names to try, in which order. Pure data: every host's list is
//! available on every host (so Tier 1 checks all of them everywhere), and
//! `platform` only picks which [`Host`] is current.

use std::ffi::OsString;

/// A driver library OBS opens at run time.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Library {
    /// The CUDA driver API (`libcuda.so.1` / `nvcuda.dll`).
    CudaDriver,
    /// The NVENC API (`libnvidia-encode.so.1` / `nvEncodeAPI64.dll`).
    NvEncode,
    /// NVML (`libnvidia-ml.so.1` / `nvml.dll`).
    Nvml,
}

impl Library {
    /// The environment variable that overrides the search for this library.
    pub fn env_override(self) -> &'static str {
        match self {
            Library::CudaDriver => "OBS_CUDA_LIBRARY",
            Library::NvEncode => "OBS_NVENC_LIBRARY",
            Library::Nvml => "OBS_NVML_LIBRARY",
        }
    }

    /// The tag the C plugin's test process reports when this library fails
    /// to load (`[NVENC] Test process failed: <tag>`).
    pub fn c_error_tag(self) -> &'static str {
        match self {
            Library::CudaDriver => "cuda_lib",
            Library::NvEncode => "nvenc_lib",
            Library::Nvml => "nvml_lib",
        }
    }
}

/// Where a candidate came from, for diagnostics.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CandidateSource {
    /// `OBS_*_LIBRARY` set by the user.
    EnvOverride,
    /// The versioned soname / DLL name the C plugin opens.
    SonameDefault,
    /// The unversioned development symlink (last resort by name).
    SonameUnversioned,
    /// An absolute path in a distribution's driver directory.
    DistroPath,
    /// The WSL driver directory.
    WslPath,
}

/// One name or path to hand to the loader.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct Candidate {
    pub name: OsString,
    pub source: CandidateSource,
}

impl Candidate {
    fn new(name: impl Into<OsString>, source: CandidateSource) -> Self {
        Candidate {
            name: name.into(),
            source,
        }
    }
}

/// The host family whose candidate list applies.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Host {
    /// Linux and other non-Apple Unix.
    Linux,
    /// Windows; `arm64` swaps the NVENC DLL order like `nvenc-load.c`.
    Windows { arm64: bool },
    /// macOS: no NVIDIA drivers, so no candidates.
    MacOs,
}

impl Host {
    /// The host this binary runs on.
    pub fn current() -> Host {
        crate::platform::HOST
    }
}

/// Read access to the environment (a seam so tests need not mutate it).
pub trait Env {
    fn var(&self, key: &str) -> Option<OsString>;
}

/// The process environment.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemEnv;

impl Env for SystemEnv {
    fn var(&self, key: &str) -> Option<OsString> {
        std::env::var_os(key)
    }
}

/// The built-in search list for `lib` on `host`, without any override.
pub fn default_candidates(lib: Library, host: Host) -> Vec<Candidate> {
    match host {
        Host::Linux => linux_candidates(lib),
        Host::Windows { arm64 } => windows_candidates(lib, arm64),
        Host::MacOs => Vec::new(),
    }
}

/// Multiarch and distribution driver directories, in search order. NixOS
/// keeps the driver in `/run/opengl-driver/lib`.
const LINUX_DISTRO_DIRS: &[&str] = &[
    "/usr/lib/x86_64-linux-gnu",
    "/usr/lib/aarch64-linux-gnu",
    "/usr/lib64",
    "/usr/lib/nvidia",
    "/run/opengl-driver/lib",
];

/// WSL exposes the Windows host's driver here.
const LINUX_WSL_DIR: &str = "/usr/lib/wsl/lib";

fn linux_candidates(lib: Library) -> Vec<Candidate> {
    // The first name is exactly what the C plugin dlopens.
    let (soname, unversioned) = match lib {
        Library::CudaDriver => ("libcuda.so.1", "libcuda.so"),
        Library::NvEncode => ("libnvidia-encode.so.1", "libnvidia-encode.so"),
        Library::Nvml => ("libnvidia-ml.so.1", "libnvidia-ml.so"),
    };
    let mut out = vec![
        Candidate::new(soname, CandidateSource::SonameDefault),
        Candidate::new(unversioned, CandidateSource::SonameUnversioned),
    ];
    out.extend(
        LINUX_DISTRO_DIRS
            .iter()
            .map(|dir| Candidate::new(format!("{dir}/{soname}"), CandidateSource::DistroPath)),
    );
    out.push(Candidate::new(
        format!("{LINUX_WSL_DIR}/{soname}"),
        CandidateSource::WslPath,
    ));
    out
}

fn windows_candidates(lib: Library, arm64: bool) -> Vec<Candidate> {
    // Bare names: the platform loader restricts the search to System32.
    let names: &[&str] = match lib {
        Library::CudaDriver => &["nvcuda.dll"],
        // nvenc-load.c: nvEncodeAPI.dll on _M_ARM64, else nvEncodeAPI64.dll.
        Library::NvEncode if arm64 => &["nvEncodeAPI.dll", "nvEncodeAPI64.dll"],
        Library::NvEncode => &["nvEncodeAPI64.dll", "nvEncodeAPI.dll"],
        Library::Nvml => &["nvml.dll"],
    };
    names
        .iter()
        .map(|n| Candidate::new(*n, CandidateSource::SonameDefault))
        .collect()
}

/// The full search list for `lib` on `host`: the env override (if set and
/// non-empty) first, then [`default_candidates`].
pub fn candidates_for(lib: Library, host: Host, env: &dyn Env) -> Vec<Candidate> {
    let mut out = Vec::new();
    if let Some(path) = env.var(lib.env_override()).filter(|p| !p.is_empty()) {
        out.push(Candidate::new(path, CandidateSource::EnvOverride));
    }
    out.extend(default_candidates(lib, host));
    out
}

/// [`candidates_for`] on the current host.
pub fn candidates(lib: Library, env: &dyn Env) -> Vec<Candidate> {
    candidates_for(lib, Host::current(), env)
}
