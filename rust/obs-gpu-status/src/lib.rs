//! GPU acceleration status for the status-bar light (obs-rust/obs-studio#846).
//!
//! [`classify`] is pure: it takes the active adapter name (as libobs logs it
//! in `Loading up OpenGL on adapter ...`), the hardware GPUs detected on the
//! system and optional frame timings, and returns green, yellow or red.
//! Detection reads a sysfs/`/dev/dri` tree under a caller-supplied root, so
//! tests use a fixture tree; [`platform::system_devices`] probes the host.

#![deny(unsafe_code)]

use std::path::Path;
use std::process::Command;

#[allow(unsafe_code)]
pub mod ffi;
pub mod platform;
pub mod report;

/// The light's color.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    /// The active adapter is a hardware GPU.
    Green,
    /// A hardware GPU is present, but rendering runs on a software renderer.
    Yellow,
    /// Software rendering with no usable GPU, graphics init failed, or
    /// frames are behind budget.
    Red,
}

/// PCI vendor of a detected GPU.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Vendor {
    Nvidia,
    Amd,
    Intel,
}

impl Vendor {
    /// Map a PCI vendor ID (`0x10de`, `10de`, ...) to a known GPU vendor.
    pub fn from_pci_id(id: &str) -> Option<Self> {
        let id = id.trim();
        let hex = id
            .strip_prefix("0x")
            .or_else(|| id.strip_prefix("0X"))
            .unwrap_or(id);
        match hex.to_ascii_lowercase().as_str() {
            "10de" => Some(Self::Nvidia),
            "1002" => Some(Self::Amd),
            "8086" => Some(Self::Intel),
            _ => None,
        }
    }
}

/// A hardware GPU found on the system.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GpuDevice {
    pub vendor: Vendor,
    /// PCI device ID, lowercase hex without `0x` (e.g. `2504`), if known.
    pub device_id: Option<String>,
    /// Marketing name, if known (from `nvidia-smi`).
    pub name: Option<String>,
    /// Whether a `/dev/dri/renderD*` node belongs to this device.
    pub render_node: bool,
}

/// Graphics-thread timings over a window, in nanoseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FrameStats {
    /// Median `obs_graphics_thread` time over the window.
    pub median_frame_ns: u64,
    /// The frame interval (budget), e.g. 33_333_333 at 30 fps.
    pub interval_ns: u64,
}

/// Fewest one-second samples [`FrameStats::from_samples`] judges a window on.
pub const MIN_FRAME_SAMPLES: usize = 5;

impl FrameStats {
    /// The median of one-second frame-time averages (`obs_get_average_frame_time_ns`
    /// sampled once a second), or `None` with fewer than [`MIN_FRAME_SAMPLES`]
    /// samples or no interval. A median keeps one slow second, such as the
    /// first one after launch, from turning the light red.
    pub fn from_samples(samples: &[u64], interval_ns: u64) -> Option<Self> {
        if interval_ns == 0 || samples.len() < MIN_FRAME_SAMPLES {
            return None;
        }
        Some(Self {
            median_frame_ns: median(samples)?,
            interval_ns,
        })
    }
}

/// The median of `values`, or `None` when it is empty. An even count gives
/// the midpoint of the two middle values, rounded down; `u64::midpoint`
/// computes it without overflowing.
pub fn median(values: &[u64]) -> Option<u64> {
    let mut sorted = values.to_vec();
    sorted.sort_unstable();
    let mid = sorted.len() / 2;
    match sorted.len() {
        0 => None,
        n if n % 2 == 1 => Some(sorted[mid]),
        _ => Some(sorted[mid - 1].midpoint(sorted[mid])),
    }
}

/// Whether `adapter` names a software renderer (llvmpipe, softpipe,
/// Microsoft Basic Render Driver, SwiftShader).
pub fn is_software_renderer(adapter: &str) -> bool {
    let a = adapter.to_ascii_lowercase();
    [
        "llvmpipe",
        "softpipe",
        "microsoft basic render driver",
        "swiftshader",
    ]
    .iter()
    .any(|s| a.contains(s))
}

/// Classify the GPU acceleration state.
///
/// `adapter` is `None` when graphics initialization failed.
pub fn classify(adapter: Option<&str>, devices: &[GpuDevice], frames: Option<FrameStats>) -> State {
    let Some(adapter) = adapter else {
        return State::Red;
    };
    if frames.is_some_and(|f| f.median_frame_ns > f.interval_ns) {
        return State::Red;
    }
    match (is_software_renderer(adapter), devices.is_empty()) {
        (false, _) => State::Green,
        (true, false) => State::Yellow,
        (true, true) => State::Red,
    }
}

/// Detect hardware GPUs from `<root>/sys/class/drm/card*/device/vendor`.
/// A device has a render node when a `<root>/sys/class/drm/renderD*` entry
/// has the same vendor and device ID and `<root>/dev/dri/renderD*` exists.
pub fn detect_devices(root: &Path) -> Vec<GpuDevice> {
    let drm = root.join("sys/class/drm");
    let Ok(entries) = std::fs::read_dir(&drm) else {
        return Vec::new();
    };
    let read_id = |dir: &Path, file: &str| -> Option<String> {
        let raw = std::fs::read_to_string(dir.join("device").join(file)).ok()?;
        let raw = raw.trim();
        Some(raw.strip_prefix("0x").unwrap_or(raw).to_ascii_lowercase())
    };
    let mut cards = Vec::new();
    let mut render_nodes = Vec::new();
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let dir = entry.path();
        // `card0` and `renderD128`, not connectors such as `card0-HDMI-A-1`.
        if let Some(n) = name.strip_prefix("card") {
            if n.bytes().all(|b| b.is_ascii_digit()) && !n.is_empty() {
                cards.push(dir);
            }
        } else if name.starts_with("renderD") && root.join("dev/dri").join(&name).exists() {
            render_nodes.push((read_id(&dir, "vendor"), read_id(&dir, "device")));
        }
    }
    cards
        .iter()
        .filter_map(|dir| {
            let vendor_id = read_id(dir, "vendor");
            let vendor = Vendor::from_pci_id(vendor_id.as_deref()?)?;
            let device_id = read_id(dir, "device");
            let render_node = render_nodes.contains(&(vendor_id, device_id.clone()));
            Some(GpuDevice {
                vendor,
                device_id,
                name: None,
                render_node,
            })
        })
        .collect()
}

/// Parse `nvidia-smi --query-gpu=name,pci.device_id --format=csv,noheader`.
///
/// Each line is `<name>, 0x<device><vendor>` (e.g. `NVIDIA GeForce RTX 3060,
/// 0x250410DE`). Lines that do not parse are skipped.
pub fn parse_nvidia_smi(output: &str) -> Vec<GpuDevice> {
    output
        .lines()
        .filter_map(|line| {
            let (name, id) = line.rsplit_once(',')?;
            let id = id.trim();
            let hex = id.strip_prefix("0x").or_else(|| id.strip_prefix("0X"))?;
            if hex.len() != 8 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
                return None;
            }
            let (device, vendor) = hex.split_at(4);
            Some(GpuDevice {
                vendor: Vendor::from_pci_id(vendor)?,
                device_id: Some(device.to_ascii_lowercase()),
                name: Some(name.trim().to_owned()),
                render_node: false,
            })
        })
        .collect()
}

/// Run `nvidia-smi`; an absent tool or a failing driver yields no devices.
pub fn query_nvidia_smi() -> Vec<GpuDevice> {
    match Command::new("nvidia-smi")
        .args(["--query-gpu=name,pci.device_id", "--format=csv,noheader"])
        .output()
    {
        Ok(out) if out.status.success() => parse_nvidia_smi(&String::from_utf8_lossy(&out.stdout)),
        _ => Vec::new(),
    }
}

/// Add `extra` devices not already in `devices` (same vendor and device ID);
/// a match fills in the missing name instead.
pub fn merge_devices(devices: &mut Vec<GpuDevice>, extra: Vec<GpuDevice>) {
    for e in extra {
        match devices
            .iter_mut()
            .find(|d| d.vendor == e.vendor && d.device_id == e.device_id && d.name.is_none())
        {
            Some(d) => d.name = e.name,
            None => devices.push(e),
        }
    }
}
