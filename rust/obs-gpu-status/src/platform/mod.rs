//! The platform boundary (obs-rust/obs-studio#323): the only place in this
//! crate that selects on the host.

use crate::GpuDevice;

cfg_select! {
    target_os = "linux" => {
        /// GPUs on this machine: the sysfs/`/dev/dri` tree, then `nvidia-smi`.
        pub fn system_devices() -> Vec<GpuDevice> {
            let mut devices = crate::detect_devices(std::path::Path::new("/"));
            crate::merge_devices(&mut devices, crate::query_nvidia_smi());
            devices
        }
    }
    _ => {
        /// GPUs on this machine. There is no sysfs here (DXGI enumeration on
        /// Windows is a follow-up), so only `nvidia-smi` is consulted.
        pub fn system_devices() -> Vec<GpuDevice> {
            crate::query_nvidia_smi()
        }
    }
}
