//! The C ABI the frontend calls (obs-rust/obs-studio#846).

use std::ffi::{CStr, c_int};

use obs_gpu_status::ffi::{obs_gpu_status_free, obs_gpu_status_report};

#[test]
fn report_round_trip() {
    let mut state: c_int = -1;
    // SAFETY: a literal C string and a local int.
    let json = unsafe {
        obs_gpu_status_report(
            c"Microsoft Basic Render Driver".as_ptr(),
            std::ptr::null(),
            0,
            0,
            &mut state,
        )
    };
    assert!(!json.is_null());
    // SAFETY: the returned string is NUL-terminated and still owned by us.
    let text = unsafe { CStr::from_ptr(json) }.to_str().unwrap().to_owned();
    // Yellow or red depending on the machine's GPUs, never green.
    assert!(state == 1 || state == 2, "{state} {text}");
    assert!(
        text.contains("\"adapter\":\"Microsoft Basic Render Driver\""),
        "{text}"
    );
    // SAFETY: from obs_gpu_status_report, freed once.
    unsafe { obs_gpu_status_free(json) };
}

#[test]
fn init_failure_and_overrun() {
    let mut state: c_int = -1;
    // SAFETY: null adapter and a local int.
    let json =
        unsafe { obs_gpu_status_report(std::ptr::null(), std::ptr::null(), 0, 0, &mut state) };
    assert_eq!(state, 2);
    // SAFETY: as above.
    unsafe { obs_gpu_status_free(json) };
    // Five one-second samples over budget: red.
    let slow = [95_000_000u64; 5];
    // SAFETY: a literal C string, a local array of `slow.len()` samples.
    let json = unsafe {
        obs_gpu_status_report(
            c"NVIDIA GeForce RTX 3060".as_ptr(),
            slow.as_ptr(),
            slow.len(),
            33_333_333,
            &mut state,
        )
    };
    assert_eq!(state, 2);
    // SAFETY: as above.
    unsafe { obs_gpu_status_free(json) };
    // One slow startup second among fast ones: green.
    let startup = [400_000_000u64, 2_000_000, 2_000_000, 2_000_000, 2_000_000];
    // SAFETY: as above.
    let json = unsafe {
        obs_gpu_status_report(
            c"NVIDIA GeForce RTX 3060".as_ptr(),
            startup.as_ptr(),
            startup.len(),
            33_333_333,
            &mut state,
        )
    };
    assert_eq!(state, 0);
    // SAFETY: as above; null is ignored too.
    unsafe {
        obs_gpu_status_free(json);
        obs_gpu_status_free(std::ptr::null_mut());
    }
}
