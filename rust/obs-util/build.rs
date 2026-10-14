fn main() {
    let windows = std::env::var_os("CARGO_CFG_WINDOWS").is_some();
    let msvc = std::env::var("CARGO_CFG_TARGET_ENV").ok().as_deref() == Some("msvc");
    if windows {
        // The plugin scan faults on a truncated image. MSVC catches that
        // with __try, as platform-windows.c does. MinGW compiles the same
        // checks without the handler.
        let mut plugin = cc::Build::new();
        plugin
            .file("src/platform/windows/plugin_seh.c")
            .warnings(true)
            .extra_warnings(true)
            .warnings_into_errors(true);
        if msvc {
            // thread_name_seh.c raises 0x406D1388 and catches it. MSVC only.
            cc::Build::new()
                .file("src/ffi/thread_name_seh.c")
                .warnings(true)
                .extra_warnings(true)
                .warnings_into_errors(true)
                .compile("thread_name_seh");
        }
        plugin.compile("plugin_seh");
    }
}
