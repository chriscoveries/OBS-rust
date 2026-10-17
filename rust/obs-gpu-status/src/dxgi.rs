//! Windows GPU detection from DXGI adapter descriptions
//! (obs-rust/obs-studio#942). The mapping is pure so it is tested on every
//! host; `platform::windows` fills [`AdapterDesc`] from `EnumAdapters1`.

use crate::{GpuDevice, Vendor};

/// `DXGI_ADAPTER_FLAG_SOFTWARE`: WARP and other software adapters.
pub const DXGI_ADAPTER_FLAG_SOFTWARE: u32 = 2;

/// PCI vendor ID Microsoft uses for its software and virtual adapters
/// (Microsoft Basic Render Driver, Hyper-V video).
pub const MICROSOFT_VENDOR_ID: u32 = 0x1414;

/// The fields of `DXGI_ADAPTER_DESC1` the light needs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AdapterDesc {
    pub description: String,
    pub vendor_id: u32,
    pub device_id: u32,
    pub flags: u32,
}

/// Hardware GPUs among `adapters`: software adapters and Microsoft's own
/// are dropped, unknown vendors too.
pub fn devices_from_dxgi(adapters: &[AdapterDesc]) -> Vec<GpuDevice> {
    adapters
        .iter()
        .filter(|a| a.flags & DXGI_ADAPTER_FLAG_SOFTWARE == 0 && a.vendor_id != MICROSOFT_VENDOR_ID)
        .filter_map(|a| {
            Some(GpuDevice {
                vendor: Vendor::from_pci_id(&format!("{:04x}", a.vendor_id))?,
                device_id: Some(format!("{:04x}", a.device_id)),
                name: Some(a.description.trim_end_matches(['\0', ' ']).to_owned()),
                // DXGI only lists adapters Windows can render on.
                render_node: true,
            })
        })
        .collect()
}
