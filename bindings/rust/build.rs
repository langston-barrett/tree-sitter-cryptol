fn main() {
    let mut c_config = cc::Build::new();
    c_config.std("c11").include("src");

    #[cfg(target_env = "msvc")]
    c_config.flag("-utf-8");

    if std::env::var("TARGET").unwrap() == "wasm32-unknown-unknown" {
        let Ok(wasm_headers) = std::env::var("DEP_TREE_SITTER_LANGUAGE_WASM_HEADERS") else {
            panic!(
                "Environment variable DEP_TREE_SITTER_LANGUAGE_WASM_HEADERS must be set by the language crate"
            );
        };

        c_config.include(&wasm_headers);
    }

    // The main grammar, and the grammars for Cryptol expressions and types
    // (see expression/ and type/), which share the main grammar's scanner.
    for dir in ["src", "expression/src", "type/src"] {
        for file in ["parser.c", "scanner.c"] {
            let path = std::path::Path::new(dir).join(file);
            c_config.file(&path);
            println!("cargo:rerun-if-changed={}", path.to_str().unwrap());
        }
    }

    c_config.compile("tree-sitter-cryptol");

    println!("cargo:rustc-check-cfg=cfg(with_highlights_query)");
    if !"queries/highlights.scm".is_empty()
        && std::path::Path::new("queries/highlights.scm").exists()
    {
        println!("cargo:rustc-cfg=with_highlights_query");
    }
    println!("cargo:rustc-check-cfg=cfg(with_injections_query)");
    if !"queries/injections.scm".is_empty()
        && std::path::Path::new("queries/injections.scm").exists()
    {
        println!("cargo:rustc-cfg=with_injections_query");
    }
    println!("cargo:rustc-check-cfg=cfg(with_locals_query)");
    if !"queries/locals.scm".is_empty() && std::path::Path::new("queries/locals.scm").exists() {
        println!("cargo:rustc-cfg=with_locals_query");
    }
    println!("cargo:rustc-check-cfg=cfg(with_tags_query)");
    if !"queries/tags.scm".is_empty() && std::path::Path::new("queries/tags.scm").exists() {
        println!("cargo:rustc-cfg=with_tags_query");
    }
}
