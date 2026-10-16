//! Host-specific non-Unicode argument tests for the preflight CLI.

cfg_select! {
    unix => {
        mod unix;
    }
    windows => {
        mod windows;
    }
    _ => {
        // Other hosts do not expose a std OsString constructor for invalid text.
        const _: () = ();
    }
}
