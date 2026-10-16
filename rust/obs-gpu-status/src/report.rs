//! What the status light and its dialog show, and what obs-websocket's
//! `GetStats` returns as `gpuAcceleration` (obs-rust/obs-studio#846).

use std::fmt::Write as _;

use crate::{FrameStats, GpuDevice, State, Vendor, classify};

/// The classified state plus the plain-words text the dialog shows.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Report {
    pub state: State,
    /// The active adapter, `None` when graphics initialization failed.
    pub adapter: Option<String>,
    pub devices: Vec<GpuDevice>,
    /// One or two sentences: what is happening and what it means.
    pub summary: String,
    /// Likely causes to check, empty when there is nothing to fix.
    pub causes: Vec<String>,
}

const SOFTWARE_CAUSES: [&str; 3] = [
    "OBS runs in a sandbox or container (Flatpak, AppImage, a Nix FHS wrapper) \
     that is missing the vendor's EGL/GLX libraries or the egl-wayland \
     external platform config (__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS).",
    "The GPU driver is missing, or its user-space libraries do not match the \
     loaded kernel driver.",
    "The /dev/dri render node is missing or not accessible to OBS \
     (log: \"Unable to open DRM render node.\").",
];

impl Report {
    pub fn new(adapter: Option<&str>, devices: Vec<GpuDevice>, frames: Option<FrameStats>) -> Self {
        let state = classify(adapter, &devices, frames);
        let mut causes = Vec::new();
        let summary = match adapter {
            None => "Graphics initialization failed, so OBS cannot render video. \
                     Check the log for the graphics error."
                .to_owned(),
            Some(a) if frames.is_some_and(|f| f.median_frame_ns > f.interval_ns) => {
                let f = frames.unwrap_or(FrameStats {
                    median_frame_ns: 0,
                    interval_ns: 0,
                });
                format!(
                    "Frames take {:.1} ms to render against a {:.1} ms budget on {a}; \
                     expect choppy video.",
                    ms(f.median_frame_ns),
                    ms(f.interval_ns)
                )
            }
            Some(a) => match state {
                State::Green => format!("Rendering on {a}."),
                State::Yellow => {
                    causes = SOFTWARE_CAUSES.iter().map(|c| (*c).to_owned()).collect();
                    let gpu = devices
                        .iter()
                        .find_map(|d| d.name.clone())
                        .unwrap_or_else(|| devices[0].vendor.display().to_owned());
                    format!(
                        "Your GPU ({gpu}) was detected, but OBS is not using its driver. \
                         Rendering on the CPU ({a}); expect choppy video."
                    )
                }
                State::Red => {
                    causes = SOFTWARE_CAUSES[1..]
                        .iter()
                        .map(|c| (*c).to_owned())
                        .collect();
                    format!(
                        "No GPU was detected and OBS is rendering on the CPU ({a}); \
                         expect choppy video."
                    )
                }
            },
        };
        Self {
            state,
            adapter: adapter.map(str::to_owned),
            devices,
            summary,
            causes,
        }
    }

    /// The `gpuAcceleration` JSON object:
    /// `{"state","adapter","detectedGpus":[{"vendor","deviceId","name","renderNode"}],"summary","causes"}`.
    pub fn to_json(&self) -> String {
        let mut j = String::from("{\"state\":");
        string(&mut j, Some(self.state.as_str()));
        j.push_str(",\"adapter\":");
        string(&mut j, self.adapter.as_deref());
        j.push_str(",\"detectedGpus\":[");
        for (i, d) in self.devices.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            j.push_str("{\"vendor\":");
            string(&mut j, Some(d.vendor.as_str()));
            j.push_str(",\"deviceId\":");
            string(&mut j, d.device_id.as_deref());
            j.push_str(",\"name\":");
            string(&mut j, d.name.as_deref());
            let _ = write!(j, ",\"renderNode\":{}}}", d.render_node);
        }
        j.push_str("],\"summary\":");
        string(&mut j, Some(&self.summary));
        j.push_str(",\"causes\":[");
        for (i, c) in self.causes.iter().enumerate() {
            if i > 0 {
                j.push(',');
            }
            string(&mut j, Some(c));
        }
        j.push_str("]}");
        j
    }
}

fn ms(ns: u64) -> f64 {
    ns as f64 / 1_000_000.0
}

/// Append `s` as a JSON string, or `null`.
fn string(out: &mut String, s: Option<&str>) {
    let Some(s) = s else {
        out.push_str("null");
        return;
    };
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c < ' ' => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out.push('"');
}

impl State {
    /// `"green"`, `"yellow"` or `"red"`.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Green => "green",
            Self::Yellow => "yellow",
            Self::Red => "red",
        }
    }
}

impl Vendor {
    /// `"nvidia"`, `"amd"` or `"intel"`.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Nvidia => "nvidia",
            Self::Amd => "amd",
            Self::Intel => "intel",
        }
    }

    fn display(self) -> &'static str {
        match self {
            Self::Nvidia => "NVIDIA",
            Self::Amd => "AMD",
            Self::Intel => "Intel",
        }
    }
}
