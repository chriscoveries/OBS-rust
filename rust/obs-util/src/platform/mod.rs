//! The platform boundary (obs-rust/obs-studio#323): the only place in the
//! safe cores that selects on the host. Each OS child holds that host's
//! native code; the rest of the crate uses the neutral items re-exported
//! here. The `ban_platform_cfg_outside_boundary` Dylint rejects host `cfg`s
//! and native OS paths (`std::os::*`, `libc::`, Win32) outside this module
//! and [`crate::ffi`].

/// `enum os_event_type`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventType {
    /// `OS_EVENT_TYPE_AUTO`: a successful wait consumes the signal.
    Auto,
    /// `OS_EVENT_TYPE_MANUAL`: the event stays signalled until `reset`.
    Manual,
}

cfg_select! {
    unix => {
        pub mod unix;
        pub use unix::{Event, Pipe, Sem, close_file, path_from_bytes};
    }
    windows => {
        #[allow(unsafe_code)] // Win32 handle and string calls; see the module docs
        pub mod windows;
        pub use windows::{Event, Pipe, Sem, close_file, path_from_bytes};
    }
}

cfg_select! {
    windows => {
        /// C `long` is 32-bit on Windows and 64-bit on the other supported targets.
        pub type CLong = i32;
        /// Atomic with the width of C `long` (`os_atomic_*_long`).
        pub type CLongAtomic = core::sync::atomic::AtomicI32;
        /// `config_save` writes a UTF-8 BOM first on Windows.
        pub const CONFIG_FILE_BOM: &[u8] = b"\xEF\xBB\xBF";
    }
    _ => {
        /// C `long` is 32-bit on Windows and 64-bit on the other supported targets.
        pub type CLong = i64;
        /// Atomic with the width of C `long` (`os_atomic_*_long`).
        pub type CLongAtomic = core::sync::atomic::AtomicI64;
        /// `config_save` writes a UTF-8 BOM first on Windows only.
        pub const CONFIG_FILE_BOM: &[u8] = b"";
    }
}

/// `util_mul_div64` uses `_umul128`/`_udiv128` on MSVC x64, which compute
/// the exact quotient (and fault when it overflows 64 bits); everywhere
/// else it uses the overflowing split formula.
pub const MUL_DIV64_IS_UDIV128: bool = cfg!(all(target_env = "msvc", target_arch = "x86_64"));
