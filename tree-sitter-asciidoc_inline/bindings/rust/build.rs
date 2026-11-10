fn main() {
    let src_dir = std::path::Path::new("src");

    let mut c_config = cc::Build::new();
    c_config.std("c11").include(src_dir);

    #[cfg(target_env = "msvc")]
    c_config.flag("-utf-8");

    let parser_path = src_dir.join("parser.c");
    c_config.file(&parser_path);
    // The scanner and its headers change without parser.c, so watch all of src.
    println!("cargo:rerun-if-changed={}", src_dir.to_str().unwrap());

    // NOTE: if your language uses an external scanner, uncomment this block:
    let scanner_path = src_dir.join("scanner.c");
    c_config.file(&scanner_path);
    c_config.warnings(false);

    c_config.compile("tree-sitter-asciidoc_inline");
}
