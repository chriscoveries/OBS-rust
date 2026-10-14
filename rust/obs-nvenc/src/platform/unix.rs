//! Linux and other non-Apple Unix: `dlopen` through libloading.

use std::ffi::c_void;

use crate::{Candidate, DynLib, DynLoader, Host, Symbol};

pub(crate) const HOST: Host = Host::Linux;

/// The real loader: `dlopen(name, RTLD_LAZY | RTLD_LOCAL)`, as `os_dlopen`.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemLoader;

struct Lib(libloading::Library);

impl DynLoader for SystemLoader {
    fn open(&self, c: &Candidate) -> Result<Box<dyn DynLib>, String> {
        // SAFETY: loading a driver library runs its initialisers. These are
        // the same NVIDIA driver libraries the C plugin dlopens today; the
        // candidate names are fixed data or an explicit user override.
        let lib = unsafe { libloading::Library::new(&c.name) }.map_err(|e| e.to_string())?;
        Ok(Box::new(Lib(lib)))
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
        type GetVersion = unsafe extern "C" fn(*mut i32) -> i32;
        let mut version = 0i32;
        // SAFETY: `sym` was resolved from this library under the name
        // `cuDriverGetVersion`, whose C type is `CUresult (int *)`. The
        // library stays loaded while `self` lives.
        let res = unsafe {
            let f: GetVersion = std::mem::transmute::<*mut c_void, GetVersion>(sym.as_ptr());
            f(&mut version)
        };
        (res, version)
    }
}
