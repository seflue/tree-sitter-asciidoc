#include "include/base_types.h"
#include "tree_sitter/parser.h"

typedef enum TokenType {
    TOKEN_TYPE_EOF,
    TOKEN_HARD_WRAP_PLUS,
    TOKEN_EMPHASIS_BEGIN,
    TOKEN_ITALIC_BEGIN,
    TOKEN_MONOSPACE_BEGIN,
    TOKEN_HIGHLIGHT_BEGIN,
    TOKEN_TYPOGRAPHIC_DOUBLE_BEGIN,
    TOKEN_TYPOGRAPHIC_SINGLE_BEGIN,
    TOKEN_PASS_DOUBLE_BEGIN,
    TOKEN_PASS_TRIPLE_BEGIN,
} TokenType;

static bool is_space(int32_t c) {
    return c == ' ' || c == '\t';
}

static bool is_word_char(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// Step over a line ending inside a span. A span may run over the lines of one
// paragraph but not past a blank line, so return false at a blank line or the
// end of input, where no close can follow.
static bool cross_line_ending(TSLexer *lexer) {
    if (lexer->lookahead == '\r') {
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
    }
    while (is_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
    return !lexer->eof(lexer) && lexer->lookahead != '\n' &&
           lexer->lookahead != '\r';
}

// Scan a constrained opening delimiter `d` (the lexer is positioned at it).
//
// AsciiDoc constrained formatting only applies when the span is balanced.
// Emit the opening delimiter token only when:
//   - the opener is immediately followed by a non-space, non-delimiter char,
//   - and a valid closing `d` exists later in the paragraph: not escaped,
//     preceded by a non-space, and followed by a word boundary.
// Otherwise return false so the delimiter falls back to plain punctuation
// instead of error-recovering into a formatting node with a missing close.
static bool scan_constrained_begin(TSLexer *lexer, int32_t d,
                                   TokenType symbol) {
    lexer->advance(lexer, false); // consume the opening delimiter
    int32_t after = lexer->lookahead;
    if (lexer->eof(lexer) || after == d || is_space(after) || after == '\n' ||
        after == '\r') {
        return false;
    }
    // The emitted token is exactly the opening delimiter.
    lexer->mark_end(lexer);

    // Look ahead for a valid close, over line endings but not past a blank line.
    int32_t prev = after;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n' || c == '\r') {
            if (!cross_line_ending(lexer)) {
                return false;
            }
            // A close needs a non-space before it; a line start has none.
            prev = ' ';
            continue;
        }
        if (c == d && prev != '\\' && !is_space(prev)) {
            lexer->advance(lexer, false);
            int32_t next = lexer->lookahead;
            // A doubled delimiter is the unconstrained form, not a close.
            if (next != d && (lexer->eof(lexer) || !is_word_char(next))) {
                lexer->result_symbol = symbol;
                return true;
            }
            prev = c;
            continue;
        }
        prev = c;
        lexer->advance(lexer, false);
    }
    return false;
}

// Scan a typographic-quote opener: a quote char (`"` for double, `'` for
// single) immediately followed by a backtick.  The emitted token is the
// 2-char opener.  Emit it only when the content starts immediately (no
// space) and the first unescaped backtick after it is followed by the same
// quote char -- i.e. a valid `<backtick><quote>` close on the same line.
// Otherwise return false so the quote falls back to plain punctuation.
static bool scan_typographic_begin(TSLexer *lexer, int32_t quote,
                                   TokenType symbol) {
    lexer->advance(lexer, false); // consume the quote char
    if (lexer->lookahead != '`') {
        return false;
    }
    lexer->advance(lexer, false); // consume the backtick
    int32_t after = lexer->lookahead;
    if (lexer->eof(lexer) || is_space(after) || after == '\n' ||
        after == '\r' || after == '`') {
        return false;
    }
    lexer->mark_end(lexer); // token is exactly the 2-char opener

    int32_t prev = after;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != '\n' &&
           lexer->lookahead != '\r') {
        if (lexer->lookahead == '`' && prev != '\\') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == quote) {
                lexer->result_symbol = symbol;
                return true;
            }
            // First backtick is not a close; the content rule stops here, so
            // there is no valid typographic quote.
            return false;
        }
        prev = lexer->lookahead;
        lexer->advance(lexer, false);
    }
    return false;
}

// Scan an unconstrained passthrough opener; the lexer is past its first `+`
// and looks at the second. Asciidoctor's InlinePassMacroRx matches
// `(\+\+\+?|\$\$)(CC_ALL*?)` closed by the same delimiter, so it needs a
// close of the same width and allows empty content: the triple form wins
// when a `+++` closes it, otherwise the double form when a `++` closes it
// (so `++++` is an empty `++` passthrough), and without a close the plus
// signs stay text.
//
// The emitted token is always `++`: whether the triple form applies is known
// only after the lookahead, when the token end can no longer move past the
// third `+`. The grammar reads that third `+` after the triple opener.
static bool scan_pass_begin(TSLexer *lexer, const bool *valid_symbols) {
    lexer->advance(lexer, false); // the second `+`
    lexer->mark_end(lexer);
    bool triple = lexer->lookahead == '+';

    // Offsets count from the start of the opener. A run of plus signs closes
    // the double form once it holds two, and the triple form once it holds
    // three that start at offset 3 or later.
    bool double_close = false;
    unsigned offset = 2, run = 0, run_start = 0;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n' || c == '\r') {
            if (!cross_line_ending(lexer)) {
                break;
            }
            run = 0;
            offset++;
            continue;
        }
        if (c == '+') {
            if (run == 0) {
                run_start = offset;
            }
            run++;
            if (run >= 2) {
                double_close = true;
            }
            if (triple && run >= 3 && run_start + run >= 6 &&
                valid_symbols[TOKEN_PASS_TRIPLE_BEGIN]) {
                lexer->result_symbol = TOKEN_PASS_TRIPLE_BEGIN;
                return true;
            }
        } else {
            run = 0;
        }
        lexer->advance(lexer, false);
        offset++;
    }
    if (double_close) {
        lexer->result_symbol = TOKEN_PASS_DOUBLE_BEGIN;
        return true;
    }
    return false;
}

void *tree_sitter_asciidoc_inline_external_scanner_create() {
    return NULL;
}

void tree_sitter_asciidoc_inline_external_scanner_destroy(void *payload) {
    // ...
}

unsigned tree_sitter_asciidoc_inline_external_scanner_serialize(void *payload,
                                                                char *buffer) {
    return 0;
}

void tree_sitter_asciidoc_inline_external_scanner_deserialize(
    void *payload, const char *buffer, unsigned length) {
    // ...
}

bool tree_sitter_asciidoc_inline_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols) {
    // Skip leading inline whitespace, kept out of any emitted token (the
    // `skip' flag), remembering whether we saw any -- a hard wrap needs a
    // preceding space.  Falling through (rather than bailing on spaces)
    // lets a constrained delimiter after a space still be recognized.
    // Spaces only count toward a hard wrap when they follow text on the
    // line; leading indentation (column 0) does not, so "    +" stays
    // punctuation while "text +" is a hard wrap.
    bool had_space = false;
    bool at_line_start = lexer->get_column(lexer) == 0;
    for (;;) {
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            if (!at_line_start) {
                had_space = true;
            }
            lexer->advance(lexer, true);
        } else if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            // A newline ends the run of spaces relevant to a hard wrap.
            had_space = false;
            at_line_start = true;
            lexer->advance(lexer, true);
        } else {
            break;
        }
    }

    // A plus sign: a trailing " +" at the end of a line is a hard wrap, and
    // `++` or `+++` may open an unconstrained passthrough.
    bool may_wrap = valid_symbols[TOKEN_HARD_WRAP_PLUS] && had_space;
    if (lexer->lookahead == '+' &&
        (may_wrap || valid_symbols[TOKEN_PASS_DOUBLE_BEGIN])) {
        lexer->advance(lexer, false);
        if (lexer->eof(lexer) || lexer->lookahead == '\n' ||
            lexer->lookahead == '\r') {
            if (!may_wrap) {
                return false;
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = TOKEN_HARD_WRAP_PLUS;
            return true;
        }
        if (lexer->lookahead == '+' && valid_symbols[TOKEN_PASS_DOUBLE_BEGIN]) {
            return scan_pass_begin(lexer, valid_symbols);
        }
        return false;
    }

    int32_t la = lexer->lookahead;
    if (valid_symbols[TOKEN_EMPHASIS_BEGIN] && la == '*') {
        return scan_constrained_begin(lexer, '*', TOKEN_EMPHASIS_BEGIN);
    }
    if (valid_symbols[TOKEN_ITALIC_BEGIN] && la == '_') {
        return scan_constrained_begin(lexer, '_', TOKEN_ITALIC_BEGIN);
    }
    if (valid_symbols[TOKEN_MONOSPACE_BEGIN] && la == '`') {
        return scan_constrained_begin(lexer, '`', TOKEN_MONOSPACE_BEGIN);
    }
    if (valid_symbols[TOKEN_HIGHLIGHT_BEGIN] && la == '#') {
        return scan_constrained_begin(lexer, '#', TOKEN_HIGHLIGHT_BEGIN);
    }
    if (valid_symbols[TOKEN_TYPOGRAPHIC_DOUBLE_BEGIN] && la == '"') {
        return scan_typographic_begin(lexer, '"',
                                      TOKEN_TYPOGRAPHIC_DOUBLE_BEGIN);
    }
    if (valid_symbols[TOKEN_TYPOGRAPHIC_SINGLE_BEGIN] && la == '\'') {
        return scan_typographic_begin(lexer, '\'',
                                      TOKEN_TYPOGRAPHIC_SINGLE_BEGIN);
    }

    if (lexer->eof(lexer)) {
        if (valid_symbols[TOKEN_TYPE_EOF]) {
            lexer->result_symbol = TOKEN_TYPE_EOF;
            return true;
        }
    }

    return false;
}
