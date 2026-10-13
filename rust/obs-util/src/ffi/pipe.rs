//! C ABI shim for `libobs/util/pipe.c` and the POSIX half,
//! `libobs/util/pipe-posix.c`.
//!
//! The process surface covers both halves: `crate::pipe::Pipe` is the
//! POSIX port on Unix and the `util/pipe-windows.c` port on Windows, with
//! the same API. The args surface is platform-neutral.

use core::ffi::{CStr, c_char};
use core::ptr;

use crate::pipe::Args;

fn bytes<'a>(ptr: *const c_char) -> Option<&'a [u8]> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: `ptr` is a C string when non-null.
    Some(unsafe { CStr::from_ptr(ptr) }.to_bytes())
}

/// # Safety
///
/// `executable` is a C string or null. A null executable returns null; C
/// would crash in `bstrdup` (listed in `tests/pipe_parity.rs`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_process_args_create(executable: *const c_char) -> *mut Args {
    let Some(executable) = bytes(executable) else {
        return ptr::null_mut();
    };
    Box::into_raw(Box::new(Args::new(executable)))
}

/// # Safety
///
/// `args` came from [`os_process_args_create`] or is null; `arg` is a C
/// string or null. Nulls are ignored; C would crash (listed in
/// `tests/pipe_parity.rs`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_process_args_add_arg(args: *mut Args, arg: *const c_char) {
    if args.is_null() {
        return;
    }
    let Some(arg) = bytes(arg) else {
        return;
    };
    // SAFETY: `args` is a live `Args` when non-null.
    unsafe { &mut *args }.add_arg(arg);
}

/// # Safety
///
/// `args` came from [`os_process_args_create`] or is null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_process_args_get_argc(args: *const Args) -> usize {
    if args.is_null() {
        return 0;
    }
    // SAFETY: `args` is a live `Args` when non-null.
    unsafe { &*args }.argc()
}

/// # Safety
///
/// `args` came from [`os_process_args_create`] or is null. The returned
/// argv stays valid until the next mutation, like the C darray buffer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_process_args_get_argv(args: *const Args) -> *mut *mut c_char {
    if args.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: `args` is a live `Args` when non-null.
    unsafe { &*args }.argv_ptr()
}

/// # Safety
///
/// `args` came from [`os_process_args_create`] or is null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn os_process_args_destroy(args: *mut Args) {
    if args.is_null() {
        return;
    }
    // SAFETY: `args` is a live `Box<Args>` we allocated when non-null.
    unsafe {
        drop(Box::from_raw(args));
    }
}

#[cfg(any(unix, windows))]
pub use process::*;

#[cfg(any(unix, windows))]
mod process {
    use super::*;
    use crate::pipe::Pipe;
    use core::ffi::c_int;

    /// Copy the current public argv view into owned arguments before spawn.
    /// The owned strings in `Args` provide storage, but callers may reorder
    /// the mutable pointer array returned by `os_process_args_get_argv`.
    ///
    /// # Safety
    /// Every non-null pointer in the public array must refer to a readable,
    /// NUL-terminated string, and the array must remain readable through
    /// `args.argc()` entries for the duration of this call.
    unsafe fn snapshot_public_argv(args: &Args) -> Option<Args> {
        let argv = args.argv_ptr();
        if argv.is_null() {
            return None;
        }

        let mut snapshot: Option<Args> = None;
        for index in 0..args.argc() {
            // SAFETY: guaranteed by this function's contract.
            let arg = unsafe { *argv.add(index) };
            if arg.is_null() {
                break;
            }
            let arg = bytes(arg)?;
            if let Some(snapshot) = snapshot.as_mut() {
                snapshot.add_arg(arg);
            } else {
                snapshot = Some(Args::new(arg));
            }
        }

        snapshot
    }

    #[cfg(windows)]
    unsafe extern "C" {
        fn blog(log_level: c_int, format: *const c_char, ...);
    }

    /// pipe-windows.c logs a failed `CreateProcessW` with its error code
    /// (and, on purpose, not the command line).
    #[cfg(windows)]
    fn log_create_process_error(code: u32) {
        // SAFETY: the format takes one `unsigned long`, which `c_ulong` is.
        unsafe {
            blog(
                crate::base::LOG_ERROR,
                c"CreateProcessW failed: %lu".as_ptr(),
                core::ffi::c_ulong::from(code),
            );
        }
    }

    /// # Safety
    ///
    /// `cmd_line` and `type_` are C strings or null. Nulls return null, as
    /// in C.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_create(
        cmd_line: *const c_char,
        type_: *const c_char,
    ) -> *mut Pipe {
        let (Some(cmd_line), Some(type_)) = (bytes(cmd_line), bytes(type_)) else {
            return ptr::null_mut();
        };
        #[cfg(unix)]
        let pipe = Pipe::create(Some(cmd_line), type_);
        #[cfg(windows)]
        let pipe = Pipe::create_logged(Some(cmd_line), type_, log_create_process_error);
        match pipe {
            Some(pipe) => Box::into_raw(Box::new(pipe)),
            None => ptr::null_mut(),
        }
    }

    /// # Safety
    ///
    /// `args` came from [`os_process_args_create`] or is null; `type_` is
    /// a C string or null. Nulls return null (C would dereference `args`).
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_create2(
        args: *const Args,
        type_: *const c_char,
    ) -> *mut Pipe {
        let Some(type_) = bytes(type_) else {
            return ptr::null_mut();
        };
        // SAFETY: `args` is a live `Args` when non-null.
        let args = if args.is_null() {
            None
        } else {
            // SAFETY: the public argv array and its non-null strings must
            // remain readable for this call, as required by the helper above.
            unsafe { snapshot_public_argv(&*args) }
        };
        #[cfg(unix)]
        let pipe = Pipe::create2(args.as_ref(), type_);
        #[cfg(windows)]
        let pipe = Pipe::create2_logged(args.as_ref(), type_, log_create_process_error);
        match pipe {
            Some(pipe) => Box::into_raw(Box::new(pipe)),
            None => ptr::null_mut(),
        }
    }

    /// # Safety
    ///
    /// `pp` came from a create function or is null.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_destroy(pp: *mut Pipe) -> c_int {
        if pp.is_null() {
            return 0;
        }
        // SAFETY: `pp` is a live `Box<Pipe>` we allocated when non-null.
        unsafe { *Box::from_raw(pp) }.destroy()
    }

    fn buf_mut<'a>(data: *mut u8, len: usize) -> Option<&'a mut [u8]> {
        if data.is_null() {
            return None;
        }
        // SAFETY: `data` covers `len` bytes when non-null.
        Some(unsafe { core::slice::from_raw_parts_mut(data, len) })
    }

    fn buf<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
        if data.is_null() {
            return None;
        }
        // SAFETY: `data` covers `len` bytes when non-null.
        Some(unsafe { core::slice::from_raw_parts(data, len) })
    }

    /// # Safety
    ///
    /// `pp` came from a create function or is null; `data` covers `len`
    /// bytes or is null (null yields 0; C would crash).
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_read(
        pp: *mut Pipe,
        data: *mut u8,
        len: usize,
    ) -> usize {
        if pp.is_null() {
            return 0;
        }
        let Some(buf) = buf_mut(data, len) else {
            return 0;
        };
        // SAFETY: `pp` is a live `Pipe` when non-null.
        unsafe { &mut *pp }.read(buf)
    }

    /// # Safety
    ///
    /// Same contract as [`os_process_pipe_read`].
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_read_err(
        pp: *mut Pipe,
        data: *mut u8,
        len: usize,
    ) -> usize {
        if pp.is_null() {
            return 0;
        }
        let Some(buf) = buf_mut(data, len) else {
            return 0;
        };
        // SAFETY: `pp` is a live `Pipe` when non-null.
        unsafe { &mut *pp }.read_err(buf)
    }

    /// # Safety
    ///
    /// Same contract as [`os_process_pipe_read`] with a read-only buffer.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn os_process_pipe_write(
        pp: *mut Pipe,
        data: *const u8,
        len: usize,
    ) -> usize {
        if pp.is_null() {
            return 0;
        }
        let Some(buf) = buf(data, len) else {
            return 0;
        };
        // SAFETY: `pp` is a live `Pipe` when non-null.
        unsafe { &mut *pp }.write(buf)
    }
}
