fn main() {
    let manifest = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let header = manifest.join("../obs-c-oracle/vendor/jansson/src");
    cc::Build::new()
        .file("src/json_decref.c")
        .include(&header)
        .warnings(false)
        .flag_if_supported("-Wno-error")
        .compile("obs_data_json_glue");
    println!("cargo:rerun-if-changed=src/json_decref.c");
    println!(
        "cargo:rerun-if-changed={}",
        header.join("jansson.h").display()
    );
}
