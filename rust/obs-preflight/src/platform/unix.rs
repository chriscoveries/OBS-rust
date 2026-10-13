#[cfg(test)]
mod tests {
    use crate::parse_args;
    use std::ffi::OsString;
    use std::fs::File;
    use std::io::ErrorKind;
    use std::os::unix::ffi::OsStringExt;

    #[test]
    fn non_unicode_trace_path_is_retained_as_an_os_path() {
        let path = OsString::from_vec(vec![0xff]);
        let parsed = parse_args([OsString::from("replay"), path.clone()].into_iter())
            .expect("trace paths do not need to be Unicode");
        assert_eq!(parsed.trace_path, path);
        assert_eq!(
            File::open(&parsed.trace_path).err().unwrap().kind(),
            ErrorKind::NotFound
        );
    }

    #[test]
    fn non_unicode_option_returns_an_input_error() {
        let error = parse_args(
            [
                OsString::from("replay"),
                OsString::from("-"),
                OsString::from_vec(vec![0xff]),
            ]
            .into_iter(),
        )
        .expect_err("options must be Unicode");
        assert_eq!(error.kind(), ErrorKind::InvalidInput);
    }
}
