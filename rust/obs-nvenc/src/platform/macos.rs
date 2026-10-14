//! macOS (and any host without NVIDIA drivers): no candidates, and a loader
//! that never opens anything, so loading fails with `NotFound` immediately.

use crate::{Candidate, DynLib, DynLoader, Host};

pub(crate) const HOST: Host = Host::MacOs;

/// The real loader on this host: NVIDIA drivers do not exist here.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemLoader;

impl DynLoader for SystemLoader {
    fn open(&self, _c: &Candidate) -> Result<Box<dyn DynLib>, String> {
        Err("NVIDIA drivers are not available on this platform".to_owned())
    }
}
