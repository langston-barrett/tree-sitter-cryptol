/**
 * @file Cryptol types, for embedding in other languages
 * @license MIT
 *
 * The same grammar as ../grammar.js, but the root is a single Cryptol type
 * (schema) rather than a module, as in SAWScript's `{| ... |}` blocks.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

import cryptol from '../grammar.js';

export default grammar(cryptol, {
  name: 'cryptol_type',

  rules: {
    source_file: $ => choice(
      $.schema,
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
