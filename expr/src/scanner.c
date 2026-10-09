// The external scanner is shared with the main Cryptol grammar.
#define SCANNER_NAME(suffix) tree_sitter_cryptol_expr_external_scanner_##suffix
#include "../../src/scanner.c" // NOLINT(bugprone-suspicious-include)
