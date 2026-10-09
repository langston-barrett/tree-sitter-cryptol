/**
 * External scanner for Cryptol.
 *
 * This implements Cryptol's layout algorithm (src/Cryptol/Parser/Layout.hs in
 * GaloisInc/cryptol), which inserts virtual block starts, separators, and
 * block ends into the token stream:
 *
 *  - `where`, `of`, `private`, and `parameter` start an implicit block at the
 *    column of the next token.  A file without a `module` header is also an
 *    implicit block.
 *  - `(`, `[`, and `{` start explicit blocks, ended by the matching closer.
 *  - Inside an implicit block, a token at the block's column that starts a
 *    line gets a separator before it.
 *  - An implicit block is ended by a less-indented token, by a closing
 *    bracket, by a `,` when there is an enclosing explicit block, or by the
 *    end of the file.
 *
 * Cryptol's layout is a pure function of the token stream (it does not
 * depend on the parser state), so this scanner emits layout tokens whether
 * or not the parser expects them.  To track explicit blocks, the scanner
 * also lexes the bracket tokens.  Comments are lexed here too, because they
 * nest and because they must not affect layout.
 */

#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wctype.h>

enum TokenType {
  LAYOUT_START,
  LAYOUT_SEMICOLON,
  LAYOUT_END,
  MODULE_START,
  PAREN_L,
  PAREN_R,
  BRACKET_L,
  BRACKET_R,
  BRACE_L,
  BRACE_R,
  COMMENT,
  ERROR_SENTINEL,
  INVALID_INDENTATION,
};

// Stack entries: non-negative values are the column of an implicit block;
// negative values are explicit blocks.
#define EXPLICIT_PAREN (-1)
#define EXPLICIT_BRACKET (-2)
#define EXPLICIT_BRACE (-3)

#define NO_COLUMN (-1)

typedef struct {
  Array(int32_t) stack;
  // When we end an implicit block before a token that starts a line, the
  // following scan happens at the same position, but no longer sees the
  // newline.  These record that token's column (using Cryptol's tab stops)
  // and its column as reported by `get_column`, which identifies the
  // position: the state persists until the scanner next produces a token,
  // possibly after the internal lexer has produced several.
  int32_t pending_column;
  int32_t pending_raw_column;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline int32_t raw_column(TSLexer *lexer) {
  return (int32_t)lexer->get_column(lexer);
}

static bool is_ident_char(int32_t c) {
  return iswalnum((wint_t)c) || c == '_' || c == '\'';
}

// The column of the innermost implicit block, or NO_COLUMN if there is none.
static int32_t innermost_implicit_column(const Scanner *s) {
  for (uint32_t i = s->stack.size; i > 0; i--) {
    int32_t entry = *array_get(&s->stack, i - 1);
    if (entry >= 0) {
      return entry;
    }
  }
  return NO_COLUMN;
}

static bool has_explicit_block(const Scanner *s) {
  for (uint32_t i = 0; i < s->stack.size; i++) {
    if (*array_get(&s->stack, i) < 0) {
      return true;
    }
  }
  return false;
}

static void consume_stars(TSLexer *lexer) {
  while (lexer->lookahead == '*') {
    advance(lexer);
  }
}

// Advance past the body of a block comment, after its opening `/*...`.
// This follows Cryptol's lexer: comments nest, and `/*/`, `/**/`, etc. are
// complete comments (even inside another comment).
static void finish_block_comment(TSLexer *lexer) {
  unsigned depth = 1;
  while (depth > 0 && !lexer->eof(lexer)) {
    if (lexer->lookahead == '/') {
      advance(lexer);
      if (lexer->lookahead == '*') {
        consume_stars(lexer);
        if (lexer->lookahead == '/') {
          advance(lexer);
        } else {
          depth++;
        }
      }
    } else if (lexer->lookahead == '*') {
      consume_stars(lexer);
      if (lexer->lookahead == '/') {
        advance(lexer);
        depth--;
      }
    } else {
      advance(lexer);
    }
  }
}

// Advance past the opening of a block comment, whose `/` has been consumed
// and whose `*` is the lookahead.  Returns true if this is a documentation
// comment.  Sets `*complete` if the comment is already finished (`/**/`).
static bool open_block_comment(TSLexer *lexer, bool *complete) {
  advance(lexer);
  unsigned stars = 1;
  while (lexer->lookahead == '*') {
    advance(lexer);
    stars++;
  }
  if (lexer->lookahead == '/') {
    advance(lexer);
    *complete = true;
    return false;
  }
  *complete = false;
  return stars == 2;
}

static void finish_line_comment(TSLexer *lexer) {
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    advance(lexer);
  }
}

// Advance over whitespace and comments.  Consumes input, so callers must have
// marked the end of their token already.
static void skip_trivia(TSLexer *lexer) {
  for (;;) {
    if (iswspace((wint_t)lexer->lookahead)) {
      advance(lexer);
    } else if (lexer->lookahead == '/') {
      advance(lexer);
      if (lexer->lookahead == '/') {
        finish_line_comment(lexer);
      } else if (lexer->lookahead == '*') {
        bool complete = false;
        open_block_comment(lexer, &complete);
        if (!complete) {
          finish_block_comment(lexer);
        }
      } else {
        return;
      }
    } else {
      return;
    }
  }
}

// Check whether the lookahead starts the given keyword.  Consumes input.
static bool scan_keyword(TSLexer *lexer, const char *keyword) {
  for (const char *p = keyword; *p; p++) {
    if (lexer->lookahead != *p) {
      return false;
    }
    advance(lexer);
  }
  return !is_ident_char(lexer->lookahead);
}

// Does the file start with a `module` or `interface module` header?
// The lookahead is at the first token of the file.  Consumes input.
static bool starts_with_module_header(TSLexer *lexer) {
  if (lexer->lookahead == 'm') {
    return scan_keyword(lexer, "module");
  }
  if (lexer->lookahead != 'i' || !scan_keyword(lexer, "interface")) {
    return false;
  }
  skip_trivia(lexer);
  return scan_keyword(lexer, "module");
}

static bool emit(TSLexer *lexer, enum TokenType token) {
  lexer->result_symbol = (TSSymbol)token;
  return true;
}

// Consume a one-character token and produce it.
static bool emit_char(TSLexer *lexer, enum TokenType token) {
  advance(lexer);
  lexer->mark_end(lexer);
  return emit(lexer, token);
}

static enum TokenType closing_token(int32_t c) {
  switch (c) {
    case ')':
      return PAREN_R;
    case ']':
      return BRACKET_R;
    default:
      return BRACE_R;
  }
}

static bool scan_bracket(Scanner *s, TSLexer *lexer, int32_t c) {
  switch (c) {
    case '(':
      array_push(&s->stack, EXPLICIT_PAREN);
      return emit_char(lexer, PAREN_L);
    case '[':
      array_push(&s->stack, EXPLICIT_BRACKET);
      return emit_char(lexer, BRACKET_L);
    case '{':
      array_push(&s->stack, EXPLICIT_BRACE);
      return emit_char(lexer, BRACE_L);
    case ')':
    case ']':
    case '}':
      // End the innermost explicit block, and any implicit blocks in it.
      // Outside of error recovery, implicit blocks were already ended.
      // Cryptol reports mismatched brackets as a parse error; here the
      // parser will see an unexpected token.
      while (s->stack.size > 0 && *array_back(&s->stack) >= 0) {
        array_pop(&s->stack);
      }
      if (s->stack.size > 0) {
        array_pop(&s->stack);
      }
      return emit_char(lexer, closing_token(c));
    default:
      return false;
  }
}

static bool scan(Scanner *s, TSLexer *lexer, const bool *valid_symbols) {
  bool recovering = valid_symbols[ERROR_SENTINEL];
  bool starting_block =
      valid_symbols[LAYOUT_START] || valid_symbols[MODULE_START];

  // Skip whitespace, tracking whether the next token starts a line and, if
  // so, its column.
  bool newline = false;
  int32_t column = 0;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == '\n') {
      newline = true;
      column = 0;
    } else if (c == '\t') {
      column = (column / 8 + 1) * 8;
    } else if (iswspace((wint_t)c)) {
      column++;
    } else {
      break;
    }
    skip(lexer);
  }
  if (!newline && s->pending_column != NO_COLUMN &&
      raw_column(lexer) == s->pending_raw_column) {
    newline = true;
    column = s->pending_column;
  }
  s->pending_column = NO_COLUMN;
  s->pending_raw_column = NO_COLUMN;

  lexer->mark_end(lexer);
  bool eof = lexer->eof(lexer);
  int32_t c = lexer->lookahead;
  int32_t top = s->stack.size > 0 ? *array_back(&s->stack) : EXPLICIT_PAREN;

  // Comments do not participate in layout, so they are lexed before making
  // any layout decision.  In particular, a block started just before a
  // comment begins at the first token after the comment.
  //
  // The exception is documentation comments, which Cryptol's layout treats
  // as tokens: one at the column of the current block gets a separator
  // before it.  (Cryptol then omits the separator before the next token, but
  // the grammar allows repeated separators instead.)
  if (c == '/') {
    advance(lexer);
    if (lexer->lookahead == '/') {
      finish_line_comment(lexer);
      lexer->mark_end(lexer);
      return emit(lexer, COMMENT);
    }
    if (lexer->lookahead == '*') {
      bool complete = false;
      bool doc = open_block_comment(lexer, &complete);
      if (doc && !recovering && !starting_block && newline && top >= 0 &&
          column == top) {
        return emit(lexer, LAYOUT_SEMICOLON);
      }
      if (!complete) {
        finish_block_comment(lexer);
      }
      lexer->mark_end(lexer);
      return emit(lexer, COMMENT);
    }
    // An operator starting with `/`.  We can no longer inspect the
    // character after it, but none of the decisions below need to.
    c = '/';
  }

  if (recovering) {
    return !eof && scan_bracket(s, lexer, c);
  }

  if (valid_symbols[MODULE_START]) {
    if (eof) {
      array_push(&s->stack, 0);
      return emit(lexer, MODULE_START);
    }
    if (!newline) {
      column = raw_column(lexer);
    }
    if (starts_with_module_header(lexer)) {
      return false;
    }
    array_push(&s->stack, column);
    return emit(lexer, MODULE_START);
  }

  if (valid_symbols[LAYOUT_START]) {
    if (eof) {
      column = 0;
    } else if (!newline) {
      column = raw_column(lexer);
    }
    array_push(&s->stack, column);
    return emit(lexer, LAYOUT_START);
  }

  if (top >= 0) {
    // Only a token that starts a line can be less indented than the
    // innermost implicit block.  Checking that only for such tokens also
    // means that we only rely on columns we computed ourselves, which
    // respect Cryptol's tab stops, unlike `get_column`.
    bool ends_block = eof || (newline && column < top) || c == ')' ||
                      c == ']' || c == '}' ||
                      (c == ',' && has_explicit_block(s));
    if (ends_block) {
      array_pop(&s->stack);
      if (newline && !eof) {
        s->pending_column = column;
        s->pending_raw_column = raw_column(lexer);
      }
      return emit(lexer, LAYOUT_END);
    }
    if (newline && column == top) {
      return emit(lexer, LAYOUT_SEMICOLON);
    }
  } else if (newline && !eof && column < innermost_implicit_column(s)) {
    // Cryptol rejects a token inside brackets that is less indented than
    // the enclosing implicit block.  The grammar never accepts this token,
    // so producing it causes a parse error here.
    return emit(lexer, INVALID_INDENTATION);
  }

  return !eof && scan_bracket(s, lexer, c);
}

void *tree_sitter_cryptol_external_scanner_create(void) {
  Scanner *s = ts_calloc(1, sizeof(Scanner));
  array_init(&s->stack);
  s->pending_column = NO_COLUMN;
  s->pending_raw_column = NO_COLUMN;
  return s;
}

void tree_sitter_cryptol_external_scanner_destroy(void *payload) {
  Scanner *s = payload;
  array_delete(&s->stack);
  ts_free(s);
}

#define HEADER_SIZE (2 * sizeof(int32_t))

unsigned tree_sitter_cryptol_external_scanner_serialize(void *payload,
                                                        char *buffer) {
  Scanner *s = payload;
  memcpy(buffer, &s->pending_column, sizeof(int32_t));
  memcpy(buffer + sizeof(int32_t), &s->pending_raw_column, sizeof(int32_t));
  uint32_t count = s->stack.size;
  uint32_t max =
      (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - HEADER_SIZE) / sizeof(int32_t);
  // If the stack is too deep, keep the innermost blocks.
  uint32_t first = count > max ? count - max : 0;
  size_t bytes = (count - first) * sizeof(int32_t);
  if (bytes > 0) {
    memcpy(buffer + HEADER_SIZE, s->stack.contents + first, bytes);
  }
  return (unsigned)(HEADER_SIZE + bytes);
}

void tree_sitter_cryptol_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *s = payload;
  array_clear(&s->stack);
  s->pending_column = NO_COLUMN;
  s->pending_raw_column = NO_COLUMN;
  if (length < HEADER_SIZE) {
    return;
  }
  memcpy(&s->pending_column, buffer, sizeof(int32_t));
  memcpy(&s->pending_raw_column, buffer + sizeof(int32_t), sizeof(int32_t));
  uint32_t count = (uint32_t)((length - HEADER_SIZE) / sizeof(int32_t));
  if (count > 0) {
    array_reserve(&s->stack, count);
    memcpy(s->stack.contents, buffer + HEADER_SIZE, count * sizeof(int32_t));
    s->stack.size = count;
  }
}

bool tree_sitter_cryptol_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  return scan(payload, lexer, valid_symbols);
}
