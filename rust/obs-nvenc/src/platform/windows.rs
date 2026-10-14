//! Windows: `LoadLibraryExW` restricted to System32, so a DLL planted in the
//! working directory or on `PATH` is never picked up.

use std::ffi::c_void;

use libloading::os::windows::{LOAD_LIBRARY_SEARCH_SYSTEM32, Library as WinLibrary};

use crate::{Candidate, DynLib, DynLoader, Host, Symbol};

pub(crate) const HOST: Host = Host::Windows {
    arm64: cfg!(target_arch = "aarch64"),
};

/// The real loader: `LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemLoader;

struct Lib(libloading::Library);

impl DynLoader for SystemLoader {
    fn open(&self, c: &Candidate) -> Result<Box<dyn DynLib>, String> {
        // SAFETY: loading a driver DLL runs its DllMain. These are the NVIDIA
        // driver DLLs the C plugin loads today, and the search is limited to
        // System32 (an absolute override path is used as given).
        let lib = unsafe { WinLibrary::load_with_flags(&c.name, LOAD_LIBRARY_SEARCH_SYSTEM32) }
            .map_err(|e| e.to_string())?;
        Ok(Box::new(Lib(lib.into())))
    }
}

impl DynLib for Lib {
    fn symbol(&self, name: &str) -> Option<Symbol> {
        // SAFETY: the symbol is read as an untyped address only; it is called
        // solely through `call_driver_get_version` with its documented type.
        let sym = unsafe { self.0.get::<*mut c_void>(name.as_bytes()) }.ok()?;
        Symbol::from_ptr(*sym)
    }

    fn call_driver_get_version(&self, sym: Symbol) -> (i32, i32) {
        // CUDAAPI is __stdcall, which `extern "system"` matches on every arch.
        type GetVersion = unsafe extern "system" fn(*mut i32) -> i32;
        let mut version = 0i32;
        // SAFETY: `sym` was resolved from this DLL under the name
        // `cuDriverGetVersion`, whose C type is `CUresult CUDAAPI (int *)`.
        // The DLL stays loaded while `self` lives.
        let res = unsafe {
            let f: GetVersion = std::mem::transmute::<*mut c_void, GetVersion>(sym.as_ptr());
            f(&mut version)
        };
        (res, version)
    }
}
