# Changelog

## Unreleased

- Add grammars for a single Cryptol expression (`cryptol_expression`, in
  `expression/`) and a single Cryptol type (`cryptol_type`, in `type/`), for
  embedding Cryptol in other languages (e.g., SAWScript's `{{ ... }}` and
  `{| ... |}` blocks). The Rust crate exports them as `LANGUAGE_EXPRESSION`
  and `LANGUAGE_TYPE`.
- Lex symbols made of operator characters (e.g., `=`, `->`, `|`, `..`) like
  Cryptol does, so that they are never parsed as operators.
- Lex comment openers like Cryptol does (e.g., `/*+` is an operator).
- Apply layout rules to tokens that follow a comment on the same line.
- The differential test now detects error recovery that inserts hidden
  tokens.

## [0.1.0] - 2026-10-09

Initial release.
