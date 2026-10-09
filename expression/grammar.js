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
    source_file: $ => choice(
      $._expr,
      // Never matches, because the scanner never produces `_unreachable`, but
      // makes every rule of the main grammar reachable, so that queries for
      // the main grammar (e.g., queries/highlights.scm) work here too.
      seq($._unreachable, $._module_body),
    ),

    _module_body: $ => choice(
      $.module,
      $.interface_module,
      seq($._module_start, optional($._top_declarations), $._layout_end),
    ),
  },
});
