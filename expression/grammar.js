/**
 * @file Cryptol expressions, for embedding in other languages
 * @license MIT
 *
 * The same grammar as ../grammar.js, but the root is a single Cryptol
 * expression rather than a module, as in SAWScript's `{{ ... }}` blocks.  As
 * when Cryptol parses an expression (rather than a module), there is no
 * implicit layout block at the top level.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

import cryptol from '../grammar.js';

export default grammar(cryptol, {
  name: 'cryptol_expression',

  rules: {
    source_file: $ => $._expr,
  },
});
