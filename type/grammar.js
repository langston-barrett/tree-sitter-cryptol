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
    source_file: $ => $.schema,
  },
});
