//! The platform boundary (obs-rust/obs-studio#323): the only place in this
//! crate that selects on the host. Each OS child provides `HOST` (which
//! candidate list [`crate::default_candidates`] uses) and `SystemLoader`, the
//! real [`crate::DynLoader`]. `unsafe` (opening a library and calling a
//! resolved symbol) is allowed only in these children.

cfg_select! {
    target_os = "macos" => {
        mod macos;
        pub use macos::SystemLoader;
        pub(crate) use macos::HOST;
    }
    unix => {
        #[allow(unsafe_code)] // dlopen via libloading and calling resolved symbols
        mod unix;
        pub use unix::SystemLoader;
        pub(crate) use unix::HOST;
    }
    windows => {
        #[allow(unsafe_code)] // LoadLibraryExW via libloading and calling resolved symbols
        mod windows;
        pub use windows::SystemLoader;
        pub(crate) use windows::HOST;
    }
    _ => {
        mod macos;
        pub use macos::SystemLoader;
        pub(crate) use macos::HOST;
    }
}
