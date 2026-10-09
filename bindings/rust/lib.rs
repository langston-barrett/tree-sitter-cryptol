//! This crate provides Cryptol language support for the [tree-sitter] parsing library.
//!
//! Typically, you will use the [`LANGUAGE`] constant to add this language to a
//! tree-sitter [`Parser`], and then use the parser to parse some code:
//!
//! ```
//! let code = r#"
//! "#;
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_cryptol::LANGUAGE;
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading Cryptol parser");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [`Parser`]: https://docs.rs/tree-sitter/0.27.1/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

unsafe extern "C" {
    fn tree_sitter_cryptol() -> *const ();
    fn tree_sitter_cryptol_expression() -> *const ();
    fn tree_sitter_cryptol_type() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for Cryptol modules (`.cry` files).
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_cryptol) };

/// The tree-sitter [`LanguageFn`] for a single Cryptol expression, e.g., as
/// embedded in SAWScript's `{{ ... }}` blocks.
pub const LANGUAGE_EXPRESSION: LanguageFn =
    unsafe { LanguageFn::from_raw(tree_sitter_cryptol_expression) };

/// The tree-sitter [`LanguageFn`] for a single Cryptol type (schema), e.g., as
/// embedded in SAWScript's `{| ... |}` blocks.
pub const LANGUAGE_TYPE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_cryptol_type) };

/// The content of the [`node-types.json`] file for [`LANGUAGE`].
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The content of the `node-types.json` file for [`LANGUAGE_EXPRESSION`].
pub const NODE_TYPES_EXPRESSION: &str = include_str!("../../expression/src/node-types.json");

/// The content of the `node-types.json` file for [`LANGUAGE_TYPE`].
pub const NODE_TYPES_TYPE: &str = include_str!("../../type/src/node-types.json");

#[cfg(with_highlights_query)]
/// The syntax highlighting query for this grammar.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

#[cfg(with_injections_query)]
/// The language injection query for this grammar.
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

#[cfg(with_locals_query)]
/// The local variable query for this grammar.
pub const LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");

#[cfg(with_tags_query)]
/// The symbol tagging query for this grammar.
pub const TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

#[cfg(test)]
mod tests {
    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::LANGUAGE.into())
            .expect("Error loading Cryptol parser");
    }

    #[test]
    fn test_can_parse_expressions_and_types() {
        let mut parser = tree_sitter::Parser::new();
        for (language, text) in [
            (super::LANGUAGE_EXPRESSION, "\\x -> x + 1"),
            (super::LANGUAGE_TYPE, "{n} (fin n) => [n] -> Bit"),
        ] {
            parser.set_language(&language.into()).unwrap();
            let tree = parser.parse(text, None).unwrap();
            assert!(!tree.root_node().has_error(), "{text}");
        }
    }
}
