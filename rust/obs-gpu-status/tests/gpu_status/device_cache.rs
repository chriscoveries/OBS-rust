//! The GPU list is probed once, and a prefetch at launch takes the probe
//! (`nvidia-smi`, tens of milliseconds) off the first report's path.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

use obs_gpu_status::{DeviceCache, GpuDevice, Vendor};

fn one_gpu() -> Vec<GpuDevice> {
    vec![GpuDevice {
        vendor: Vendor::Nvidia,
        device_id: Some("2504".into()),
        name: Some("NVIDIA GeForce RTX 3060".into()),
        render_node: true,
    }]
}

#[test]
fn devices_are_probed_once() {
    let cache = DeviceCache::new();
    let calls = AtomicUsize::new(0);
    for _ in 0..3 {
        let devices = cache.get(|| {
            calls.fetch_add(1, Ordering::SeqCst);
            one_gpu()
        });
        assert_eq!(devices.len(), 1);
    }
    assert_eq!(calls.load(Ordering::SeqCst), 1);
}

static PREFETCHED: DeviceCache = DeviceCache::new();
static SLOW_PROBES: AtomicUsize = AtomicUsize::new(0);

fn slow_probe() -> Vec<GpuDevice> {
    SLOW_PROBES.fetch_add(1, Ordering::SeqCst);
    std::thread::sleep(Duration::from_millis(100));
    one_gpu()
}

#[test]
fn a_report_during_the_prefetch_waits_for_it_instead_of_probing_again() {
    PREFETCHED.prefetch(slow_probe);
    let devices = PREFETCHED.get(|| panic!("the prefetch is already probing"));
    assert_eq!(devices.len(), 1);
    assert_eq!(devices[0].name.as_deref(), Some("NVIDIA GeForce RTX 3060"));
    assert_eq!(SLOW_PROBES.load(Ordering::SeqCst), 1);
}
