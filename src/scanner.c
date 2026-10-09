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
 *
 * Finally, this scanner lexes symbols made of operator characters (e.g., `=`,
 * `->`, `|`, `..`) and other operators.  Like Cryptol's lexer, it reads the
 * longest sequence of operator characters and then classifies it, so that,
 * e.g., `=` is never an operator, even where the parser does not expect `=`.
 * (Tree-sitter's lexer only considers tokens that the parser expects.)
 */

#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wctype.h>

// The grammars for Cryptol expressions and types (in expr/ and type/)
// share this scanner; they define SCANNER_NAME before including this file.
#ifndef SCANNER_NAME
#define SCANNER_NAME(suffix) tree_sitter_cryptol_external_scanner_##suffix
#endif

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
  // Symbols made of operator characters, and other operators.
  LAMBDA,
  ARROW_R,
  ARROW_L,
  FAT_ARROW,
  EQUALS,
  COLON,
  DOT_DOT,
  DOT_DOT_DOT,
  DOT_DOT_LT,
  DOT_DOT_GT,
  BAR,
  TRI_L,
  TRI_R,
  LT,
  GT,
  PLUS,
  MINUS,
  STAR,
  EXP,
  HASH,
  AT,
  TILDE,
  OPERATOR,
  // Never produced (see expr/grammar.js and type/grammar.js).
  UNREACHABLE,
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

// A position in a line, for tracking where comments end.  The column uses
// Cryptol's 8-column tab stops, and is meaningful only after a newline.
typedef struct {
  int32_t column;
  bool newline;
} LinePosition;

static void advance_tracking(TSLexer *lexer, LinePosition *position) {
  if (position != NULL) {
    if (lexer->lookahead == '\n') {
      position->newline = true;
      position->column = 0;
    } else if (lexer->lookahead == '\t') {
      position->column = (position->column / 8 + 1) * 8;
    } else {
      position->column++;
    }
  }
  advance(lexer);
}

// Advance past the body of a block comment, after its opening `/*...`.
// This follows Cryptol's lexer: comments nest, and `/*/`, `/**/`, etc. are
// complete comments (even inside another comment).  If `position` is not
// NULL, it is updated to the end of the comment.
static void finish_block_comment(TSLexer *lexer, LinePosition *position) {
  unsigned depth = 1;
  while (depth > 0 && !lexer->eof(lexer)) {
    if (lexer->lookahead == '/') {
      advance_tracking(lexer, position);
      if (lexer->lookahead == '*') {
        while (lexer->lookahead == '*') {
          advance_tracking(lexer, position);
        }
        if (lexer->lookahead == '/') {
          advance_tracking(lexer, position);
        } else {
          depth++;
        }
      }
    } else if (lexer->lookahead == '*') {
      while (lexer->lookahead == '*') {
        advance_tracking(lexer, position);
      }
      if (lexer->lookahead == '/') {
        advance_tracking(lexer, position);
        depth--;
      }
    } else {
      advance_tracking(lexer, position);
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
          finish_block_comment(lexer, NULL);
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

// ASCII operator characters (Lexer.x: @op).  Cryptol also classifies
// non-ASCII symbols and punctuation as operator characters; operators that
// start with those are lexed by the grammar's `operator` rule instead.
static bool is_op_char(int32_t c) {
  return c != 0 && c < 0x80 && strchr("!#$%&*+-./:<=>?@\\^|~", (int)c);
}

// Longer runs of operator characters are not compared with the symbols.
#define MAX_SYMBOL_LENGTH 8

typedef struct {
  const char *text;
  enum TokenType token;
} Symbol;

static const Symbol SYMBOLS[] = {
    {"\\", LAMBDA},      {"->", ARROW_R},
    {"<-", ARROW_L},     {"=>", FAT_ARROW},
    {"=", EQUALS},       {":", COLON},
    {"..", DOT_DOT},     {"...", DOT_DOT_DOT},
    {"..<", DOT_DOT_LT}, {"..>", DOT_DOT_GT},
    {"|", BAR},          {"<|", TRI_L},
    {"|>", TRI_R},       {"<", LT},
    {">", GT},           {"+", PLUS},
    {"-", MINUS},        {"*", STAR},
    {"^^", EXP},         {"#", HASH},
    {"@", AT},           {"~", TILDE},
};

static enum TokenType classify_operator(const char *text) {
  for (size_t i = 0; i < sizeof(SYMBOLS) / sizeof(SYMBOLS[0]); i++) {
    if (strcmp(text, SYMBOLS[i].text) == 0) {
      return SYMBOLS[i].token;
    }
  }
  return OPERATOR;
}

// Cryptol's layout compares the column of every token with that of the
// current implicit block, but the scanner otherwise does so only for tokens
// that start a line.  Other tokens can be at or left of the block's column
// only if they follow a comment that starts a line or spans lines, e.g.:
//
//     x = 1 /*
//     */y = 2
//
// So after such a comment, if the next token is on the same line and at or
// left of the block's column, record its position as starting a line.  The
// next scan then produces a layout token there, which also clears this
// state, so it cannot apply to a later token.
static void note_token_after_comment(Scanner *s, TSLexer *lexer,
                                     LinePosition *end, int32_t top) {
  if (!end->newline || top < 0) {
    return;
  }
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
    advance_tracking(lexer, end);
  }
  int32_t c = lexer->lookahead;
  if (lexer->eof(lexer) || iswspace((wint_t)c) || c == '/' ||
      end->column > top) {
    return;
  }
  s->pending_column = end->column;
  s->pending_raw_column = raw_column(lexer);
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
        (void)array_pop(&s->stack);
      }
      if (s->stack.size > 0) {
        (void)array_pop(&s->stack);
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

  // `get_column` at the start of the token, when needed for layout.  This
  // must be computed before reading an operator below, after which the lexer
  // is no longer at the start of the token.  (`get_column` takes time
  // proportional to the column, so it is not computed for every token.)
  int32_t start_raw_column = NO_COLUMN;

  // Read a sequence of operator characters, which may be a comment, a
  // symbol, an operator, or (just `.`) the start of a selector.
  enum TokenType op_token = OPERATOR;
  bool have_op = false;
  bool selector = false;
  if (!eof && is_op_char(c)) {
    if (newline || starting_block) {
      start_raw_column = raw_column(lexer);
    }
    char text[MAX_SYMBOL_LENGTH + 1];
    size_t length = 0;
    // Whether the whole sequence is a comment opener: `/*`, `/**`, etc., or
    // a complete comment `/*/`, `/**/`, etc.  Following Cryptol's lexer, a
    // longer sequence starting with `/*` is an operator.
    enum {
      OPEN_SLASH,
      OPEN_STAR,
      OPEN_STARS,
      OPEN_CLOSED,
      NOT_OPENER
    } opener = OPEN_SLASH;
    while (is_op_char(lexer->lookahead)) {
      int32_t next = lexer->lookahead;
      if (length < MAX_SYMBOL_LENGTH) {
        text[length] = (char)next;
      }
      switch (opener) {
        case OPEN_SLASH:
          opener = next == '/' ? OPEN_STAR : NOT_OPENER;
          break;
        case OPEN_STAR:
          opener = next == '*' ? OPEN_STARS : NOT_OPENER;
          break;
        case OPEN_STARS:
          if (next == '/') {
            opener = OPEN_CLOSED;
          } else if (next != '*') {
            opener = NOT_OPENER;
          }
          break;
        default:
          opener = NOT_OPENER;
          break;
      }
      length++;
      advance(lexer);
    }
    size_t stored = length < MAX_SYMBOL_LENGTH ? length : MAX_SYMBOL_LENGTH;
    text[stored] = '\0';

    // Comments do not participate in layout, so they are produced before
    // making any layout decision.  In particular, a block started just before
    // a comment begins at the first token after the comment.
    //
    // The exception is documentation comments, which Cryptol's layout treats
    // as tokens: one at the column of the current block gets a separator
    // before it.  (Cryptol then omits the separator before the next token,
    // but the grammar allows repeated separators instead.)
    if (length >= 2 && text[0] == '/' && text[1] == '/') {
      finish_line_comment(lexer);
      lexer->mark_end(lexer);
      return emit(lexer, COMMENT);
    }
    if (opener == OPEN_STARS || opener == OPEN_CLOSED) {
      bool complete = opener == OPEN_CLOSED;
      bool doc = !complete && length == 3;
      if (doc && !recovering && !starting_block && newline && top >= 0 &&
          column == top) {
        return emit(lexer, LAYOUT_SEMICOLON);
      }
      LinePosition end = {.column = column + (int32_t)length,
                          .newline = newline};
      if (!complete) {
        finish_block_comment(lexer, &end);
      }
      lexer->mark_end(lexer);
      if (!recovering) {
        note_token_after_comment(s, lexer, &end, top);
      }
      return emit(lexer, COMMENT);
    }

    if (length == 1 && text[0] == '.' && is_ident_char(lexer->lookahead)) {
      selector = true;
    } else {
      have_op = true;
      op_token =
          length <= MAX_SYMBOL_LENGTH ? classify_operator(text) : OPERATOR;
    }
  }

  // Produce the operator (or let the grammar lex the selector).
#define FINISH_OPERATOR()                                                      \
  do {                                                                         \
    if (selector) {                                                            \
      return false;                                                            \
    }                                                                          \
    if (have_op) {                                                             \
      lexer->mark_end(lexer);                                                  \
      return emit(lexer, op_token);                                            \
    }                                                                          \
  } while (0)

  if (start_raw_column == NO_COLUMN && !have_op && !selector &&
      (starting_block || newline)) {
    start_raw_column = raw_column(lexer);
  }

  if (recovering) {
    FINISH_OPERATOR();
    return !eof && scan_bracket(s, lexer, c);
  }

  if (valid_symbols[MODULE_START]) {
    if (eof) {
      array_push(&s->stack, 0);
      return emit(lexer, MODULE_START);
    }
    if (!newline) {
      column = start_raw_column;
    }
    if (!have_op && !selector && starts_with_module_header(lexer)) {
      return false;
    }
    array_push(&s->stack, column);
    return emit(lexer, MODULE_START);
  }

  if (valid_symbols[LAYOUT_START]) {
    if (eof) {
      column = 0;
    } else if (!newline) {
      column = start_raw_column;
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
      (void)array_pop(&s->stack);
      if (newline && !eof) {
        s->pending_column = column;
        s->pending_raw_column = start_raw_column != NO_COLUMN
                                    ? start_raw_column
                                    : raw_column(lexer);
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

  FINISH_OPERATOR();
#undef FINISH_OPERATOR
  return !eof && scan_bracket(s, lexer, c);
}

void *SCANNER_NAME(create)(void) {
  Scanner *s = ts_calloc(1, sizeof(Scanner));
  array_init(&s->stack);
  s->pending_column = NO_COLUMN;
  s->pending_raw_column = NO_COLUMN;
  return s;
}

void SCANNER_NAME(destroy)(void *payload) {
  Scanner *s = payload;
  array_delete(&s->stack);
  ts_free(s);
}

#define HEADER_SIZE (2 * sizeof(int32_t))

unsigned SCANNER_NAME(serialize)(void *payload, char *buffer) {
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

void SCANNER_NAME(deserialize)(void *payload, const char *buffer,
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

bool SCANNER_NAME(scan)(void *payload, TSLexer *lexer,
                        const bool *valid_symbols) {
  return scan(payload, lexer, valid_symbols);
}
