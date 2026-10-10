use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    // No canonicalize(): on Windows it yields a verbatim `\\?\` path, and
    // MSVC cannot resolve `#include "util/..."` against such an include dir.
    // CARGO_MANIFEST_DIR is already absolute.
    let libobs = manifest
        .ancestors()
        .nth(2)
        .expect("rust/obs-c-oracle has a repo root two levels up")
        .join("libobs");

    let mut oracle = cc::Build::new();
    // Rust never fuses `a * b + c` into an FMA, but GCC/Clang may (clang's
    // default -ffp-contract=on does so on aarch64), which would make float
    // oracles such as vec2_norm (`x*x + y*y`) differ in the last bit from the
    // Rust port. Pin the C oracle to unfused IEEE operations. MSVC does not
    // contract under its default /fp:precise.
    if !oracle.get_compiler().is_like_msvc() {
        oracle.flag("-ffp-contract=off");
    }
    oracle
        .file("oracle/bitstream.c")
        .file("oracle/path_extension.c")
        .file("oracle/array_serializer.c")
        .file("oracle/darray.c")
        .file("oracle/crc32.c")
        .file("oracle/vec2.c")
        .file("oracle/base.c")
        .file("oracle/base_drive.c")
        .file(libobs.join("util/base-variadic.c"))
        .file("oracle/file_serializer.c")
        .file("oracle/file_serializer_host.c")
        .file(libobs.join("util/dstr.c"))
        .file("oracle/video_frame.c")
        .include(&libobs)
        .std("c11");
    // base.c includes util/threading.h, which includes <pthread.h>. MSVC has
    // none; libobs builds against the pthreads-win32 headers in
    // deps/w32-pthreads, so the oracle does too. Only declarations are used.
    if oracle.get_compiler().is_like_msvc() {
        oracle.include(libobs.with_file_name("deps").join("w32-pthreads"));
    }
    oracle.compile("obs_c_oracle");

    // Test allocator, whole-archive so bmalloc/bfree resolve regardless of
    // link order relative to obs-util.
    cc::Build::new()
        .file("oracle/test_bmem.c")
        .include(&libobs)
        .std("c11")
        .link_lib_modifier("+whole-archive")
        .compile("obs_c_oracle_bmem");

    println!("cargo:rerun-if-changed=oracle");
    for header in [
        "util/bitstream.c",
        "util/bitstream.h",
        "util/path-extension.c",
        "util/array-serializer.c",
        "util/array-serializer.h",
        "util/darray.h",
        "util/serializer.h",
        "util/bmem.h",
        "util/crc32.c",
        "util/crc32.h",
        "graphics/vec2.c",
        "graphics/vec2.h",
        "graphics/math-defs.h",
        "graphics/math-extra.h",
        "util/base.c",
        "util/base.h",
        "util/base-variadic.c",
        "util/c99defs.h",
        "util/threading.h",
        "util/file-serializer.c",
        "util/file-serializer.h",
        "util/dstr.c",
        "util/dstr.h",
        "util/platform.h",
        "media-io/video-frame.c",
        "media-io/video-frame.h",
        "media-io/video-io.h",
        "media-io/media-io-defs.h",
    ] {
        println!("cargo:rerun-if-changed={}", libobs.join(header).display());
    }
}
