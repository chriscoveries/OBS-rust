fn main() {
    for d in obs_gpu_status::platform::system_devices() {
        println!("{d:?}");
    }
}
