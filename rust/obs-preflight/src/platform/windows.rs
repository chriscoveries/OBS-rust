#[cfg(test)]
mod tests {
    use crate::parse_args;
    use std::ffi::OsString;
    use std::io::ErrorKind;
    use std::os::windows::ffi::OsStringExt;

    #[test]
    fn non_unicode_trace_path_is_retained_as_an_os_path() {
        let path = OsString::from_wide(&[0xd800]);
        let parsed = parse_args([OsString::from("replay"), path.clone()].into_iter())
            .expect("trace paths do not need to be Unicode");
        assert_eq!(parsed.trace_path, path);
    }

    #[test]
    fn non_unicode_option_returns_an_input_error() {
        let error = parse_args(
            [
                OsString::from("replay"),
                OsString::from("-"),
                OsString::from_wide(&[0xd800]),
            ]
            .into_iter(),
        )
        .expect_err("options must be Unicode");
        assert_eq!(error.kind(), ErrorKind::InvalidInput);
    }
}
