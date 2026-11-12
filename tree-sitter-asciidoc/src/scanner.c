#include "include/scanner.h"
#include "include/base_types.h"
#include "include/quick_buffer.h"
#include "include/utils.h"
#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#ifndef LIST_INIT_SIZE
#define LIST_INIT_SIZE 10
#endif  // ifndef LIST_INIT_SIZE

#ifndef LIST_GROW_SIZE
#define LIST_GROW_SIZE 10
#endif  // ifndef LIST_GROW_SIZE

void *tree_sitter_asciidoc_external_scanner_create() {
    Scanner *scanner = (Scanner *)ts_malloc(sizeof(Scanner));
    scanner_init(scanner);
    return scanner;
}

void tree_sitter_asciidoc_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    scanner_free(scanner);
    ts_free(scanner);
}

unsigned tree_sitter_asciidoc_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;

    QuickBuffer qb = quick_buffer_new(buffer, TREE_SITTER_SERIALIZATION_BUFFER_SIZE);

    scanner_serialize(scanner, &qb);

    return qb.pos;
}

void tree_sitter_asciidoc_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *s = (Scanner *)payload;
    s->len = 0;
    s->header_pending = false;

    if(!buffer || length == 0) {
        return;
    }

    QuickBuffer qb = quick_buffer_new((void *)buffer, length);
    scanner_deserialize(s, &qb);
}

bool tree_sitter_asciidoc_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *s = (Scanner *)payload;

    if(lexer->eof(lexer)) {
        if(valid_symbols[TOKEN_TYPE_EOF]) {
            lexer->result_symbol = TOKEN_TYPE_EOF;
            return true;
        }
    }

    // Whether the table opened with a header row was settled while its `|===` was scanned,
    // so this only has to hand the answer over. Nothing is consumed either way, which is
    // what lets the rest of the scanner run untouched when there is no header row.
    if(valid_symbols[TOKEN_TABLE_HEADER_START] && s->header_pending) {
        s->header_pending = false;
        lexer->mark_end(lexer);
        lexer->result_symbol = TOKEN_TABLE_HEADER_START;
        return true;
    }

    usize start_pos = lexer->get_column(lexer);

    if(valid_symbols[TOKEN_BLOCK_COMMENT_END_MARKER]) {
        if(start_pos != 0) {
            return false;
        }
        if(parse_sequence(lexer, "////")) {
            if(is_newline(lexer->lookahead)) {
                lexer->result_symbol = TOKEN_BLOCK_COMMENT_END_MARKER;
                return true;
            }
        }

        return false;
    }

    // The line ending inside a paragraph joins the next line to it only when that
    // line is prose. A blank line, the end of input or a line that could open a
    // block is left to `_block_end`, which ends the paragraph. A paragraph
    // attached to a list item asks for the list variant of the token, because
    // it also ends at a list line. In error recovery every symbol is valid,
    // and the block-comment check above has already returned, so this never
    // runs then.
    if((valid_symbols[TOKEN_PARAGRAPH_SOFT_BREAK] || valid_symbols[TOKEN_LIST_PARAGRAPH_SOFT_BREAK]) &&
       is_newline(lexer->lookahead)) {
        bool in_list = valid_symbols[TOKEN_LIST_PARAGRAPH_SOFT_BREAK];
        skip_line_ending(lexer);
        lexer->mark_end(lexer);
        if(continues_paragraph(lexer, in_list)) {
            lexer->result_symbol = in_list ? TOKEN_LIST_PARAGRAPH_SOFT_BREAK : TOKEN_PARAGRAPH_SOFT_BREAK;
            return true;
        }
        return false;
    }

    if(start_pos == 0) {
        // Read before the admonition probe: a partial label match such as the
        // `T` of `TIP` advances the lexer, and the macro-name check below must
        // see the first character of the line, not the one after the match.
        bool is_alpha_lower = is_ascii_alpha_lower(lexer->lookahead);
        bool is_digit = is_ascii_digit(lexer->lookahead);
        if(valid_symbols[TOKEN_ADMONITION_NOTE]) {
            do {
                if(parse_sequence(lexer, "NOTE")) {
                    lexer->result_symbol = TOKEN_ADMONITION_NOTE;
                } else if(parse_sequence(lexer, "TIP")) {
                    lexer->result_symbol = TOKEN_ADMONITION_TIP;
                } else if(parse_sequence(lexer, "IMPORTANT")) {
                    lexer->result_symbol = TOKEN_ADMONITION_IMPORTANT;
                } else if(parse_sequence(lexer, "CAUTION")) {
                    lexer->result_symbol = TOKEN_ADMONITION_CAUTION;
                } else if(parse_sequence(lexer, "WARNING")) {
                    lexer->result_symbol = TOKEN_ADMONITION_WARNING;
                } else {
                    break;
                }

                // mark_end caps the token at the keyword word; the colon and
                // space are only lookahead.  Advance them with skip=false: a
                // skip=true advance after mark_end would collapse the token to
                // a zero-width marker (the label would fall outside the tree).
                lexer->mark_end(lexer);
                if(lexer->lookahead == ':') {
                    lexer->advance(lexer, false);
                    if(lexer->lookahead == ' ') {
                        return true;
                    }
                }
            } while(0);
        }

        if(valid_symbols[TOKEN_LIST_MARKER_ALPHA]) {
            bool dot_read;
            if(parse_ordered_marker(lexer, &dot_read)) {
                return true;
            }
            // The digits of `2+|` or `2.3+|` were read as a list label; in a table they begin
            // the next cell's specifier.
            if(valid_symbols[TOKEN_CELL_ATTR] && is_digit) {
                CellSpecStart from = dot_read ? CELL_SPEC_AFTER_DIGITS_DOT : CELL_SPEC_AFTER_DIGITS;
                if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, from))) {
                    return true;
                }
            }
        }

        if(is_alpha_lower && scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME]) {
                if(parse_sequence(lexer, "include")) {
                    lexer->mark_end(lexer);
                    if(parse_sequence(lexer, "::") && scan_rest_is_block_macro(lexer, false)) {
                        lexer->result_symbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        if(is_alpha_lower && !scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] || valid_symbols[TOKEN_TERM]) {
                while(is_ascii_alpha_lower(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                lexer->mark_end(lexer);
                i32 c = lexer->lookahead;
                if(c == ':' || c == ';') {
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool followed_ws = is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer);
                    bool desc_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    // `name:: ` (delimiter followed by space/EOL) is a
                    // description-list term, whereas `name::target[]` is a
                    // block macro.  mark_end sits after the bare word, so the
                    // delimiter run we just consumed is only lookahead.
                    if(valid_symbols[TOKEN_TERM] && desc_run && followed_ws) {
                        lexer->result_symbol = TOKEN_TERM;
                        return true;
                    }
                    if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] && c == ':' && run >= 2 &&
                       scan_rest_is_block_macro(lexer, run > 2)) {
                        lexer->result_symbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        // One letter read by the checks above and then `|`: the style of the next cell, as in
        // `a|`.
        if(valid_symbols[TOKEN_CELL_ATTR] && is_alpha_lower && lexer->get_column(lexer) == 1) {
            if(finish_cell_attr(lexer, valid_symbols, 1)) {
                return true;
            }
        }

        if(lexer->get_column(lexer) == 0) {
            switch(lexer->lookahead) {
                case '=': {
                    consume('=', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    if(!scanner_is_matching_raw_block(s)) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_new_line(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DELIMITED, counter)) {
                                lexer->result_symbol = TOKEN_DELIMITED_BLOCK_END_MARKER;
                                scanner_pop(s);
                            } else {
                                scanner_push(s, BLOCK_KIND_DELIMITED, counter);
                                lexer->result_symbol = TOKEN_DELIMITED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }

                    if(!scanner_is_matching_raw_block(s)) {
                        usize level = TOKEN_TITLE_H0_MARKER - 1 + lexer->get_column(lexer);
                        if(level <= TOKEN_TITLE_H5_MARKER && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = level;
                            return true;
                        }
                    }
                    break;
                }
                case '#': {
                    // Markdown-style ATX heading (compat syntax): a run of `#`
                    // maps to the same section levels as `=` (`#` -> level 0,
                    // `######` -> level 5), requiring a space before the title.
                    consume('#', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    if(!scanner_is_matching_raw_block(s)) {
                        usize level = TOKEN_TITLE_H0_MARKER - 1 + lexer->get_column(lexer);
                        if(level <= TOKEN_TITLE_H5_MARKER && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = level;
                            return true;
                        }
                    }
                    break;
                }
                case '*': {
                    usize counter = 0;
                    consume('*', lexer, false, &counter, USIZE_MAX);
                    lexer->mark_end(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);

                    if(valid_symbols[TOKEN_SIDEBAR_BLOCK_START_MARKER] || valid_symbols[TOKEN_SIDEBAR_BLOCK_END_MARKER]) {
                        usize col = lexer->get_column(lexer);
                        if(col >= 4 && (is_new_line(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_SIDEBAR, col)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_SIDEBAR_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_SIDEBAR, col);
                                    lexer->result_symbol = TOKEN_SIDEBAR_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '*') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_STAR]) {
                        lexer->result_symbol = TOKEN_LIST_MARKER_STAR;
                        return is_unordered_marker;
                    }

                    break;
                }
                case '-': {
                    usize counter = 0;
                    consume('-', lexer, false, &counter, USIZE_MAX);
                    lexer->mark_end(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);
                    if(valid_symbols[TOKEN_OPEN_BLOCK_MARKER]) {
                        if(lexer->get_column(lexer) == 2 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_OPEN_BLOCK_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_QUOTED_PARAGRAPH_MARKER]) {
                        if(lexer->get_column(lexer) == 2 && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_QUOTED_PARAGRAPH_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LISTING_BLOCK_START_MARKER] || valid_symbols[TOKEN_LISTING_BLOCK_END_MARKER]) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LISTING, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LISTING_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_LISTING, counter);
                                    lexer->result_symbol = TOKEN_LISTING_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '-') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_HYPHEN] && is_unordered_marker) {
                        lexer->result_symbol = TOKEN_LIST_MARKER_HYPHEN;
                        return true;
                    }
                    break;
                }
                case '`': {
                    // Markdown-style fenced code block (compat): a line of
                    // 3+ backticks, optionally followed by an info string
                    // (language) on the opening fence.  It reuses the listing
                    // block markers but is tracked with a dedicated stack kind
                    // so a backtick fence never closes a `----` block of the
                    // same length.
                    usize counter = 0;
                    consume('`', lexer, false, &counter, USIZE_MAX);
                    if((valid_symbols[TOKEN_LISTING_BLOCK_START_MARKER] ||
                        valid_symbols[TOKEN_LISTING_BLOCK_END_MARKER]) &&
                       counter >= 3) {
                        if(scanner_is_matching(s, BLOCK_KIND_FENCED, counter)) {
                            // Closing fence: bare backticks only.
                            if(is_newline(lexer->lookahead) || is_eof(lexer)) {
                                lexer->mark_end(lexer);
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LISTING_BLOCK_END_MARKER;
                                return true;
                            }
                        } else if(!scanner_is_matching_raw_block(s)) {
                            // Opening fence: pull the rest of the line (the
                            // info string) into the marker so the body starts
                            // on the next line.
                            while(!is_newline(lexer->lookahead) && !is_eof(lexer)) {
                                lexer->advance(lexer, false);
                            }
                            lexer->mark_end(lexer);
                            scanner_push(s, BLOCK_KIND_FENCED, counter);
                            lexer->result_symbol = TOKEN_LISTING_BLOCK_START_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '.': {
                    consume('.', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    usize dots = lexer->get_column(lexer);
                    if(valid_symbols[TOKEN_LITERAL_BLOCK_MARKER]) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LITERAL, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            } else if(!scanner_is_matching_raw_block(s)) {
                                scanner_push(s, BLOCK_KIND_LITERAL, counter);
                                lexer->result_symbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_DOT]) {
                        if(is_white_space(lexer->lookahead)) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_LIST_MARKER_DOT;
                            return true;
                        }
                    }

                    // `.2+|` and `.^|` open a cell; anything else after a single `.` stays a
                    // block title, whose marker `mark_end` above has already fixed at the `.`.
                    if(valid_symbols[TOKEN_CELL_ATTR] && dots == 1 &&
                       (is_ascii_digit(lexer->lookahead) || is_cell_align(lexer->lookahead))) {
                        if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, CELL_SPEC_AFTER_DOT))) {
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_BLOCK_TITLE_MARKER]) {
                        if(dots == 1) {
                            skip_rest_of_line(lexer);
                            lexer->result_symbol = section_marker(valid_symbols, TOKEN_SECTION_TITLE_MARKER_1,
                                                                  peek_heading_level(lexer), TOKEN_BLOCK_TITLE_MARKER);
                            return true;
                        }
                    }

                    break;
                }
                case ':': {  // DSV table (:===) or document attribute (:name:)
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);  // document-attr marker is just the colon
                    if(valid_symbols[TOKEN_DSV_TABLE_BLOCK_MARKER] && lexer->lookahead == '=') {
                        // Count the `=` run by hand rather than with consume(),
                        // which would move mark_end past the colon and corrupt
                        // the document-attr fallback below.
                        usize counter = 0;
                        while(lexer->lookahead == '=') {
                            lexer->advance(lexer, false);
                            ++counter;
                        }
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_DSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    // A line that is no attribute entry stays paragraph text.
                    // A `=` run read by the DSV probe above opens no name,
                    // since `=` is not a word character.
                    if(valid_symbols[TOKEN_DOCUMENT_ATTR_MARKER] && lexer->get_column(lexer) == 1 &&
                       rest_of_line_is_attr_entry(lexer)) {
                        lexer->result_symbol = TOKEN_DOCUMENT_ATTR_MARKER;
                        return true;
                    }
                    break;
                }
                case '[': {
                    if(valid_symbols[TOKEN_ELEMENT_ATTR_MARKER] || valid_symbols[TOKEN_BLOCK_ANCHOR_MARKER]) {
                        lexer->advance(lexer, false);
                        // `[[` is a block anchor, not an attribute list.  `[#`
                        // is the id shorthand (`[#id]`) and is handled by the
                        // grammar, so it must not be rejected here.
                        if(lexer->lookahead == '[') {
                            if(!valid_symbols[TOKEN_BLOCK_ANCHOR_MARKER]) {
                                return false;
                            }
                            lexer->advance(lexer, false);
                            lexer->mark_end(lexer);
                            // A line that is no valid anchor, or an anchor with no
                            // block after it, stays paragraph text.
                            if(!rest_of_line_is_block_anchor(lexer)) {
                                return false;
                            }
                            int level = peek_heading_level(lexer);
                            if(level < 0) {
                                return false;
                            }
                            lexer->result_symbol = section_marker(valid_symbols, TOKEN_SECTION_ANCHOR_MARKER_1, level,
                                                                  TOKEN_BLOCK_ANCHOR_MARKER);
                            return true;
                        }
                        if(!valid_symbols[TOKEN_ELEMENT_ATTR_MARKER]) {
                            return false;
                        }
                        lexer->mark_end(lexer);
                        skip_rest_of_line(lexer);
                        lexer->result_symbol = section_marker(valid_symbols, TOKEN_SECTION_ATTR_MARKER_1,
                                                              peek_heading_level(lexer), TOKEN_ELEMENT_ATTR_MARKER);
                        return true;
                    }
                    break;
                }
                case '\'': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('\'', lexer)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    break;
                }
                case '<': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('<', lexer)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    // What of `<`, `<.` or `<.>` has been read, for a left-aligned cell such
                    // as `<|` or `<.^|` once neither a break nor a callout matched.
                    CellSpecStart spec_from = CELL_SPEC_AFTER_ALIGN;
                    bool spec_possible = lexer->get_column(lexer) == 1;
                    if(valid_symbols[TOKEN_CALLOUT_LIST_MARKER]) {
                        if(lexer->get_column(lexer) == 1) {
                            if(lexer->lookahead == '.') {
                                spec_from = CELL_SPEC_AFTER_ALIGN_DOT;
                            }
                            if(parse_sequence(lexer, ".>")) {
                                spec_from = CELL_SPEC_AFTER_ALIGNMENT;
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_CALLOUT_LIST_MARKER;
                                if(is_white_space(lexer->lookahead)) {
                                    return true;
                                }
                            }
                            if(parse_number(lexer)) {
                                spec_possible = false;
                                if(lexer->lookahead == '>') {
                                    lexer->advance(lexer, false);
                                    lexer->mark_end(lexer);
                                    if(is_white_space(lexer->lookahead)) {
                                        lexer->result_symbol = TOKEN_CALLOUT_LIST_MARKER;
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                    if(valid_symbols[TOKEN_CELL_ATTR] && spec_possible) {
                        if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, spec_from))) {
                            return true;
                        }
                    }
                    break;
                }
                case ',': {  // CSV table
                    if(valid_symbols[TOKEN_CSV_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_CSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '|': {  // Table
                    if(valid_symbols[TOKEN_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_TABLE, counter);
                                s->header_pending = scan_opens_with_header_row(lexer);
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '!': {  // NTable
                    if(valid_symbols[TOKEN_NTABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        consume('=', lexer, false, NULL, USIZE_MAX);
                        if(is_newline(lexer->lookahead)) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_NTABLE_BLOCK_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '/': {
                    if(valid_symbols[TOKEN_LINE_COMMENT_MARKER] ||
                       valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                        if(parse_sequence(lexer, "//")) {
                            lexer->mark_end(lexer);
                            if(valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                                if(parse_sequence(lexer, "//")) {
                                    if(is_newline(lexer->lookahead)) {
                                        lexer->result_symbol = TOKEN_BLOCK_COMMENT_START_MARKER;
                                        lexer->mark_end(lexer);
                                        return true;
                                    }
                                }
                            }
                            lexer->result_symbol = TOKEN_LINE_COMMENT_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '_': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_START_MARKER] || valid_symbols[TOKEN_QUOTED_BLOCK_END_MARKER]) {
                        consume('_', lexer, false, NULL, USIZE_MAX);
                        lexer->mark_end(lexer);
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && (is_newline(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_QUOTED, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_QUOTED_BLOCK_END_MARKER;
                            } else {
                                scanner_push(s, BLOCK_KIND_QUOTED, counter);
                                lexer->result_symbol = TOKEN_QUOTED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }
                    break;
                }
                case '>': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_MD_MARKER]) {
                        lexer->advance(lexer, false);
                        lexer->mark_end(lexer);
                        lexer->result_symbol = TOKEN_QUOTED_BLOCK_MD_MARKER;
                        if(is_white_space(lexer->lookahead) || is_eof(lexer) || is_newline(lexer->lookahead)) {
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_CELL_ATTR]) {
                        // `>|`, `>.^|`: a right-aligned cell, not a Markdown quote.
                        CellSpecStart from = lexer->get_column(lexer) == 1 ? CELL_SPEC_AFTER_ALIGN : CELL_SPEC_START;
                        if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, from))) {
                            return true;
                        }
                    }
                    break;
                }
                case '+': {
                    lexer->advance(lexer, false);
                    if(valid_symbols[TOKEN_LIST_CONTINUATION]) {
                        if(is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_LIST_CONTINUATION;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_PASSTHROUGH_BLOCK_MARKER]) {
                        if(parse_sequence(lexer, "+++")) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_PASSTHROUGH_BLOCK_MARKER;
                            if(is_new_line(lexer->lookahead) || is_eof(lexer)) {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        if(
            lexer->get_column(lexer) == 0 && valid_symbols[TOKEN_IDENT_MARKER] && is_white_space(lexer->lookahead)
        ) {
            skip_white_space(lexer);
            if(!is_new_line(lexer->lookahead) && !is_eof(lexer)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = TOKEN_IDENT_MARKER;
                return true;
            }
        }

        // Description-list term: the run of text at the start of a line that
        // is terminated by a description marker (`::`/`:::`/`::::`/`;;`
        // followed by whitespace or a line break), e.g. the `CPU` in
        // `CPU:: the brain`.  This is probed last, so every other line-start
        // construct gets first refusal; the emitted token spans from column 0
        // (where the scan began) to just before the delimiter run regardless
        // of how far earlier probes advanced the cursor.  When no marker is
        // found it returns false and the line stays an ordinary paragraph.
        // `^` opens no other line-start construct, so a cell specifier like `^|` would only
        // reach the end-of-scan check after the term probe below has read the whole line.
        if(valid_symbols[TOKEN_CELL_ATTR] && lexer->get_column(lexer) == 0 && lexer->lookahead == '^') {
            if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, CELL_SPEC_START))) {
                return true;
            }
        }

        // What the probes above read of the line, as `2+` in `2+:: def`, is term text.
        if(valid_symbols[TOKEN_TERM] && !scanner_is_matching_raw_block(s)) {
            usize term_len = lexer->get_column(lexer);
            while(!is_newline(lexer->lookahead) && !is_eof(lexer)) {
                i32 c = lexer->lookahead;
                if((c == ':' || c == ';') && term_len > 0) {
                    lexer->mark_end(lexer);
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool valid_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
                        lexer->result_symbol = TOKEN_TERM;
                        return true;
                    }
                    term_len += run;
                    continue;
                }
                lexer->advance(lexer, false);
                ++term_len;
            }
            return false;
        }
    }

    // Description-list marker: `::`, `:::`, `::::` or `;;`, always followed
    // by whitespace or a line break.  It is only valid once a term has been
    // read, i.e. mid-line, so the column-0 block above is skipped and the
    // lexer sits on the delimiter run.  A run that is not a valid marker
    // (e.g. a lone `:` in a URL or `a::b` without trailing space) returns
    // false so the delimiter falls back to ordinary term text.
    if(valid_symbols[TOKEN_DESCRIPTION_MARKER] &&
       (lexer->lookahead == ':' || lexer->lookahead == ';')) {
        i32 delim = lexer->lookahead;
        usize run = 0;
        while(lexer->lookahead == delim) {
            lexer->advance(lexer, false);
            ++run;
        }
        bool valid_run = (delim == ':' && run >= 2 && run <= 4) || (delim == ';' && run == 2);
        if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
            lexer->mark_end(lexer);
            lexer->result_symbol = TOKEN_DESCRIPTION_MARKER;
            return true;
        }
        return false;
    }

    if(valid_symbols[TOKEN_CALLOUT_MARKER]) {
        parse_sequence(lexer, "#");
        parse_sequence(lexer, "//");
        parse_sequence(lexer, ";;");
        skip_white_space(lexer);

        if(parse_sequence(lexer, "<.>")) {
            lexer->mark_end(lexer);
            if(is_newline(lexer->lookahead)) {
                lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                return true;
            }
        }

        if(parse_number(lexer)) {
            if(lexer->lookahead == '>') {
                lexer->advance(lexer, false);
                lexer->mark_end(lexer);
                if(is_newline(lexer->lookahead)) {
                    lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                    return true;
                }
            }
        }

        if(parse_sequence(lexer, "!--")) {
            if(lexer->lookahead == '.' || parse_number(lexer)) {
                if(parse_sequence(lexer, "-->")) {
                    lexer->mark_end(lexer);
                    if(is_new_line(lexer->lookahead)) {
                        lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                        return true;
                    }
                }
            }
        }
    }

    if(
        start_pos == lexer->get_column(lexer) &&
        valid_symbols[TOKEN_CELL_ATTR]
    ) {
        if(finish_cell_attr(lexer, valid_symbols, scan_cell_spec_from(lexer, CELL_SPEC_START))) {
            return true;
        }
    }

    return false;
}

/// Advance to the end of the current line.
static inline void skip_rest_of_line(TSLexer *lexer) {
    while(!lexer->eof(lexer) && !is_newline(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
}

/// Whether an id may start with `ch` (Asciidoctor: `[\p{Alpha}_:]`). Any
/// non-ASCII character counts as a letter.
static inline bool is_anchor_id_start(i32 ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == ':' || ch > 0x7f;
}

/// Whether an id may continue with `ch` (Asciidoctor: `[\p{Word}\-:.]`).
static inline bool is_anchor_id_char(i32 ch) {
    return is_anchor_id_start(ch) || is_ascii_digit(ch) || ch == '-' || ch == '.';
}

/// With the lexer just past the leading `:`, read the rest of the line and say
/// whether it completes an attribute entry: Asciidoctor's AttributeEntryRx,
/// `:` + optional `!` + a word character + anything but `:` + `:`, then the end
/// of the line or a space or tab before the value.
static inline bool rest_of_line_is_attr_entry(TSLexer *lexer) {
    if(lexer->lookahead == '!') {
        lexer->advance(lexer, false);
    }
    if(!is_word_start(lexer->lookahead) && lexer->lookahead != '_') {
        return false;
    }
    while(!lexer->eof(lexer) && !is_newline(lexer->lookahead) && lexer->lookahead != ':') {
        lexer->advance(lexer, false);
    }
    if(lexer->lookahead != ':') {
        return false;
    }
    lexer->advance(lexer, false);
    return lexer->eof(lexer) || is_newline(lexer->lookahead) || is_white_space(lexer->lookahead);
}

/// With the lexer just past `[[`, read the rest of the line and say whether
/// it completes a block anchor: Asciidoctor's BlockAnchorRx,
/// `[[` + (nothing | id + optional `,` reftext) + `]]` at the end of the line.
/// A reference text holding `]` is not accepted, because the grammar cannot
/// tell it from the closing `]]`.
static inline bool rest_of_line_is_block_anchor(TSLexer *lexer) {
    if(is_anchor_id_start(lexer->lookahead)) {
        while(is_anchor_id_char(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        if(lexer->lookahead == ',') {
            lexer->advance(lexer, false);
            while(lexer->lookahead == ' ') {
                lexer->advance(lexer, false);
            }
            usize reftext = 0;
            while(!lexer->eof(lexer) && !is_newline(lexer->lookahead) && lexer->lookahead != ']') {
                lexer->advance(lexer, false);
                reftext++;
            }
            if(reftext == 0) {
                return false;
            }
        }
    }
    if(lexer->lookahead != ']') {
        return false;
    }
    lexer->advance(lexer, false);
    if(lexer->lookahead != ']') {
        return false;
    }
    lexer->advance(lexer, false);
    return lexer->eof(lexer) || is_newline(lexer->lookahead);
}

/// With the lexer at the end of a block attribute, anchor or title line, skip
/// the rest of the run (more such lines, line comments and blank lines) and
/// return the level (1-5) of the section heading below it, 0 if something
/// else follows, or -1 if nothing does.
static inline int peek_heading_level(TSLexer *lexer) {
    for(;;) {
        if(!skip_line_ending(lexer) || lexer->eof(lexer)) {
            return -1;
        }
        i32 c = lexer->lookahead;
        if(is_newline(c)) {
            continue;
        }
        if(c == '=' || c == '#') {
            int run = 0;
            while(lexer->lookahead == c) {
                lexer->advance(lexer, false);
                run++;
            }
            return run >= 2 && run <= 6 && is_white_space(lexer->lookahead) ? run - 1 : 0;
        }
        if(c == '[') {
            lexer->advance(lexer, false);
            if(lexer->lookahead == '[') {
                lexer->advance(lexer, false);
                if(!rest_of_line_is_block_anchor(lexer)) {
                    return 0;
                }
                continue;
            }
            i32 last = c;
            while(!lexer->eof(lexer) && !is_newline(lexer->lookahead)) {
                last = lexer->lookahead;
                lexer->advance(lexer, false);
            }
            if(last != ']') {
                return 0;
            }
            continue;
        }
        if(c == '.') {
            // A block title, not a list item, literal block or a lone dot.
            lexer->advance(lexer, false);
            if(lexer->eof(lexer) || lexer->lookahead == '.' || is_white_space(lexer->lookahead) ||
               is_newline(lexer->lookahead)) {
                return 0;
            }
            skip_rest_of_line(lexer);
            continue;
        }
        if(c == '/') {
            // A line comment or a block comment.
            lexer->advance(lexer, false);
            if(lexer->lookahead != '/') {
                return 0;
            }
            lexer->advance(lexer, false);
            if(lexer->lookahead == '/') {
                if(!skip_block_comment(lexer)) {
                    return 0;
                }
                continue;
            }
            skip_rest_of_line(lexer);
            continue;
        }
        return 0;
    }
}

/// With the lexer after the first `//` of a line, skip a block comment as the
/// grammar reads it (`////` lines around the body) and stop at the end of its
/// closing line. False if the line opens no block comment or nothing closes it.
static inline bool skip_block_comment(TSLexer *lexer) {
    if(!parse_sequence(lexer, "//") || !is_newline(lexer->lookahead)) {
        return false;
    }
    while(skip_line_ending(lexer) && !lexer->eof(lexer)) {
        if(parse_sequence(lexer, "////") && is_newline(lexer->lookahead)) {
            return true;
        }
        skip_rest_of_line(lexer);
    }
    return false;
}

/// The level-tagged variant of a run's first marker for a heading of `level`,
/// when the parser can take it here, otherwise the plain `fallback` marker.
static inline TokenType section_marker(const bool *valid_symbols, TokenType first, int level, TokenType fallback) {
    if(level >= 1 && level <= 5 && valid_symbols[first + level - 1]) {
        return first + level - 1;
    }
    return fallback;
}

/// `dot_read` reports whether the `.` after the label was consumed, which a caller reading the
/// line on as a cell specifier needs when the marker turns out not to be one.
bool parse_ordered_marker(TSLexer *lexer, bool *dot_read) {
    *dot_read = false;
    if(lexer->get_column(lexer) != 0) {
        return false;
    }

    if(is_ascii_digit(lexer->lookahead)) {
        while(is_ascii_digit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        lexer->result_symbol = TOKEN_LIST_MARKER_DIGIT;
    } else if(is_ascii_alpha_lower(lexer->lookahead)) {
        lexer->result_symbol = TOKEN_LIST_MARKER_ALPHA;
        lexer->advance(lexer, false);
    } else if(is_geek_lower(lexer->lookahead)) {
        lexer->result_symbol = TOKEN_LIST_MARKER_GEEK;
        lexer->advance(lexer, false);
    } else {
        return false;
    }

    if(lexer->lookahead != '.') {
        return false;
    }
    lexer->advance(lexer, false);
    *dot_read = true;
    if(!is_white_space(lexer->lookahead)) {
        return false;
    }
    lexer->mark_end(lexer);
    return true;
}

static inline bool is_white_space(i32 ch) {
    return ch == ' ' || ch == '\t';
}

static inline bool is_ascii_digit(i32 ch) {
    return ch >= '0' && ch <= '9';
}

static inline bool is_ascii_alpha_lower(i32 ch) {
    return ch >= 'a' && ch <= 'z';
}

static inline bool is_geek_lower(i32 ch) {
    return ch >= 945 && ch <= 969;
}

static inline bool parse_breaks(char start, TSLexer *lexer) {
    i32 counter = 0;

    while(lexer->lookahead == start) {
        lexer->advance(lexer, false);
        skip_white_space(lexer);
        ++counter;
    }

    lexer->mark_end(lexer);

    return counter >= 3 && is_newline(lexer->lookahead);
}

static inline bool skip_white_space(TSLexer *lexer) {
    bool has_skiped = false;
    while(is_white_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_skiped = true;
    }

    return has_skiped;
}

static inline bool is_newline(i32 ch) {
    return ch == '\r' || ch == '\n';
}

/// Consume one line ending, if there is one.
static inline bool skip_line_ending(TSLexer *lexer) {
    if(lexer->lookahead != '\r' && lexer->lookahead != '\n') {
        return false;
    }
    if(lexer->lookahead == '\r') {
        lexer->advance(lexer, false);
    }
    if(lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
    }
    return true;
}

/// AsciiDoc promotes a table's first line to its header row when a blank line follows it and
/// the first line after the gap opens a new cell.
///
/// Called with the lexer sitting on the line ending of an opening `|===`, whose token has
/// already been marked, so everything read here is lookahead and widens nothing.
static inline bool scan_opens_with_header_row(TSLexer *lexer) {
    if(!skip_line_ending(lexer)) {
        return false;
    }

    // The line has to be cells, not the delimiter closing an empty table, which a blank
    // line may equally well follow.
    bool closes_the_table = lexer->lookahead == '|';
    usize delimiter_run = 0;

    bool first_line_has_content = false;
    while(!is_eof(lexer) && !is_newline(lexer->lookahead)) {
        first_line_has_content = true;
        if(closes_the_table && !is_white_space(lexer->lookahead)) {
            if(lexer->lookahead == '=' && delimiter_run < USIZE_MAX) {
                ++delimiter_run;
            } else if(delimiter_run > 0) {
                closes_the_table = false;
            }
        }
        lexer->advance(lexer, false);
    }
    if(!first_line_has_content || (closes_the_table && delimiter_run >= 3)) {
        return false;
    }
    if(!skip_line_ending(lexer)) {
        return false;
    }

    while(is_white_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
    if(!is_newline(lexer->lookahead)) {
        return false;
    }

    // Asciidoctor drops the header again when the first line after the gap continues the
    // first row's last cell instead of opening a new one: `a|` followed by a blank line and
    // a paragraph is one AsciiDoc cell, not a header row.  Line comments are skipped, as
    // Asciidoctor drops them from a table before reading its cells.
    for(;;) {
        while(is_white_space(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        if(skip_line_ending(lexer)) {
            continue;
        }
        if(lexer->lookahead != '/') {
            break;
        }
        lexer->advance(lexer, false);
        if(lexer->lookahead != '/') {
            return false;
        }
        lexer->advance(lexer, false);
        if(lexer->lookahead == '/') {
            return false;
        }
        skip_rest_of_line(lexer);
    }
    if(is_eof(lexer)) {
        return true;
    }
    return scan_cell_spec_from(lexer, CELL_SPEC_START) >= 0 && lexer->lookahead == '|';
}

/// A block macro is a whole line `name::target[attrs]`, close to how Asciidoctor matches it: the
/// target neither starts with a space nor has one directly before the first `[`, and the line ends
/// with the `]` that closes the attribute list.  Any other line is paragraph text, so a macro still
/// being typed does not open a block that never closes.
///
/// Called with the lexer past the colons after the name, which has been marked, so everything read
/// here is lookahead and widens nothing.  `target_started` says the colon run was longer than
/// `::`, so the target already began with a `:` and may be followed by a space.
static inline bool scan_rest_is_block_macro(TSLexer *lexer, bool target_started) {
    if(!target_started && is_white_space(lexer->lookahead)) {
        return false;
    }

    bool seen_open = false;
    i32 prev = 0;
    i32 last = 0;
    while(!is_eof(lexer) && !is_newline(lexer->lookahead)) {
        i32 ch = lexer->lookahead;
        if(ch == '[' && !seen_open) {
            if(is_white_space(prev)) {
                return false;
            }
            seen_open = true;
        }
        if(!is_white_space(ch)) {
            last = ch;
        }
        prev = ch;
        lexer->advance(lexer, false);
    }
    return seen_open && last == ']';
}

static inline bool is_eof(TSLexer *lexer) {
    return lexer->eof(lexer);
}

static inline bool is_new_line(i32 ch) {
    return ch == '\r' || ch == '\n';
}

static inline bool consume(i32 ch, TSLexer *lexer, bool skip_space, usize *counter, usize max) {
    bool has_space = false;
    if(skip_space) {
        has_space |= skip_white_space(lexer);
    }

    while(lexer->lookahead == ch) {
        lexer->advance(lexer, false);
        if(skip_space) {
            has_space |= skip_white_space(lexer);
        }
        --max;
        if(counter != NULL) {
            *counter += 1;
        }
        if(max == 0) {
            break;
        }
    }

    lexer->mark_end(lexer);
    return has_space;
}

static inline bool parse_sequence_impl(TSLexer *lexer, char const *sequence, usize len) {
    usize pos = 0;

    while(pos < len) {
        if(lexer->lookahead != sequence[pos]) {
            return false;
        }

        lexer->advance(lexer, false);
        ++pos;
    }

    return true;
}

static inline bool parse_number(TSLexer *lexer) {
    bool has_number = false;
    while(is_ascii_digit(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_number = true;
    }
    return has_number;
}

static inline bool is_cell_align(i32 ch) {
    return ch == '<' || ch == '^' || ch == '>';
}

/// A cell specifier as Asciidoctor's `CellSpecStartRx` reads it, in this order and each part
/// optional: a span or duplication factor (`2+`, `.3+`, `2.3+`, `2.+`, `3*`), an alignment
/// (`<`, `^.>`, `<.`, `.^`), a style letter (any of `a`-`z`; unknown styles are ignored but
/// still mark a cell).  Returns -1 if what was read cannot be a specifier, 0 if nothing was
/// read, 1 if a non-empty specifier was read; the caller checks for the `|` that must follow.
///
/// `from` says how much of the specifier a line-start check has already consumed before its
/// own token failed, so that a line such as `2+|`, `.2+|` or `>|` still opens a cell.
static inline int scan_cell_spec_from(TSLexer *lexer, CellSpecStart from) {
    bool read = false;
    bool digits = false;
    bool dot = false;
    bool align_open = false;  // one alignment character read; `.` may follow
    bool align_dot = false;   // `<.` read; a second alignment character may follow
    bool align_done = false;
    switch(from) {
        case CELL_SPEC_START:
            digits = parse_number(lexer);
            if(lexer->lookahead == '.') {
                lexer->advance(lexer, false);
                dot = true;
                digits = parse_number(lexer) || digits;
            }
            break;
        case CELL_SPEC_AFTER_DIGITS:
            digits = true;
            if(lexer->lookahead == '.') {
                lexer->advance(lexer, false);
                dot = true;
                parse_number(lexer);
            }
            break;
        case CELL_SPEC_AFTER_DIGITS_DOT:
            digits = true;
            dot = true;
            parse_number(lexer);
            break;
        case CELL_SPEC_AFTER_DOT:
            dot = true;
            digits = parse_number(lexer);
            break;
        case CELL_SPEC_AFTER_ALIGN:
            read = align_open = true;
            break;
        case CELL_SPEC_AFTER_ALIGN_DOT:
            read = align_dot = true;
            break;
        case CELL_SPEC_AFTER_ALIGNMENT:
            read = align_done = true;
            break;
    }

    if(!align_open && !align_dot && !align_done) {
        if(digits) {
            if(lexer->lookahead != '+' && lexer->lookahead != '*') {
                return -1;
            }
            lexer->advance(lexer, false);
            read = true;
            dot = false;
        }
        if(dot) {
            // A `.` that began no span begins a vertical alignment, as in `.^`.
            if(!is_cell_align(lexer->lookahead)) {
                return -1;
            }
            lexer->advance(lexer, false);
            read = true;
        } else if(is_cell_align(lexer->lookahead)) {
            lexer->advance(lexer, false);
            read = align_open = true;
        } else if(lexer->lookahead == '.') {
            lexer->advance(lexer, false);
            if(!is_cell_align(lexer->lookahead)) {
                return -1;
            }
            lexer->advance(lexer, false);
            read = true;
        }
    }
    if(align_open && lexer->lookahead == '.') {
        lexer->advance(lexer, false);
        align_dot = true;
    }
    if(align_dot && is_cell_align(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }

    if(is_ascii_alpha_lower(lexer->lookahead)) {
        lexer->advance(lexer, false);
        read = true;
    }
    return read ? 1 : 0;
}

/// Ends a `TOKEN_CELL_ATTR` here if `spec` read a specifier and the separator of the table it
/// sits in follows: `|`, or `!` in a nested table.  Inside a `|===` cell, where the closing
/// `|===` is valid, `x!` is text; inside a `!===` table, where only its own `!===` closes, so is
/// `x|`.  Where neither marker tells (a header row, error recovery) both separators count.
static inline bool finish_cell_attr(TSLexer *lexer, const bool *valid_symbols, int spec) {
    bool in_psv = valid_symbols[TOKEN_TABLE_BLOCK_MARKER];
    bool in_nested = valid_symbols[TOKEN_NTABLE_BLOCK_MARKER] && !in_psv;
    if(spec > 0 && ((lexer->lookahead == '|' && !in_nested) || (lexer->lookahead == '!' && !in_psv))) {
        lexer->mark_end(lexer);
        lexer->result_symbol = TOKEN_CELL_ATTR;
        return true;
    }
    return false;
}

static inline bool scanner_is_matching(Scanner const *self, BlockKind kind, usize counter) {
    Node *top = scanner_top(self);
    if(!top) {
        return false;
    }

    if(counter == 0) {
        return top->kind == kind;
    }
    return top->kind == kind && top->counter == counter;
}
static inline Result scanner_serialize(Scanner const *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    ret &= quick_buffer_write_usize(qb, self->len);
    ret &= quick_buffer_extend_bytes(qb, self->buffer, sizeof(Node) * self->len);
    ret &= quick_buffer_write_usize(qb, self->header_pending ? 1 : 0);

    return ret;
}

static inline Result scanner_deserialize(Scanner *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    usize len = 0;
    ret &= quick_buffer_read_usize(qb, &len);
    if(self->capacity < len) {
        self->buffer = ts_realloc(self->buffer, len * sizeof(Node));
        self->capacity = len;
    }
    self->len = len;
    ret &= quick_buffer_read_bytes(qb, self->buffer, len * sizeof(Node));

    usize header_pending = 0;
    ret &= quick_buffer_read_usize(qb, &header_pending);
    self->header_pending = header_pending != 0;

    return ret;
}

static inline bool scanner_pop_kind(Scanner *self, BlockKind kind, usize counter) {
    if(scanner_is_matching(self, kind, counter)) {
        scanner_pop(self);
        return true;
    }

    return false;
}

static inline void scanner_pop(Scanner *self) {
    if(self->len == 0) {
        return;
    }
    self->len -= 1;
}

static inline void scanner_push(Scanner *self, BlockKind kind, usize counter) {
    const usize BUFFER_SIZE = TREE_SITTER_SERIALIZATION_BUFFER_SIZE - sizeof(usize);

    if(self->len == self->capacity) {
        usize grow_counter = BUFFER_SIZE / sizeof(Node) - self->capacity;
        if(grow_counter == 0) {
            return;
        }
        if(LIST_GROW_SIZE < grow_counter) {
            grow_counter = LIST_GROW_SIZE;
        }

        self->capacity = self->capacity + grow_counter;
        self->buffer = ts_realloc(self->buffer, sizeof(Node) * self->capacity);
    }

    Node *top = &(self->buffer[self->len++]);
    top->kind = kind;
    top->counter = counter;
}

static inline void scanner_init(Scanner *self) {
    bzero(self, sizeof(Scanner));
    self->buffer = ts_malloc(LIST_INIT_SIZE * sizeof(Node));
    self->capacity = LIST_INIT_SIZE;
}

static inline void scanner_free(Scanner *self) {
    ts_free(self->buffer);
    bzero(self, sizeof(Scanner));
}

static inline bool scanner_is_matching_raw_block(Scanner *self) {
    return scanner_is_matching(self, BLOCK_KIND_LISTING, 0) ||
           scanner_is_matching(self, BLOCK_KIND_FENCED, 0) ||
           scanner_is_matching(self, BLOCK_KIND_LITERAL, 0);
}

static inline Node *scanner_top(Scanner const *self) {
    if(self->len == 0) {
        return NULL;
    }

    return &(self->buffer[self->len - 1]);
}

// The longest line `continues_paragraph` reads. A longer line is not judged and so
// ends the paragraph, as every line did before paragraphs spanned lines.
#define PARAGRAPH_PEEK 512

/// Whether `ch` can begin a word: an ASCII letter or digit, or any non-ASCII
/// character, which covers letters in other scripts.
static inline bool is_word_start(i32 ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || is_ascii_digit(ch) ||
           ch > 0x7f;
}

/// Reads the line the lexer is on, which the caller has already marked as lookahead,
/// and says whether it is prose that continues a paragraph.
static inline bool continues_paragraph(TSLexer *lexer, bool in_list) {
    i32 line[PARAGRAPH_PEEK];
    usize len = 0;
    while(!lexer->eof(lexer) && !is_newline(lexer->lookahead)) {
        if(len == PARAGRAPH_PEEK) {
            return false;
        }
        line[len++] = lexer->lookahead;
        lexer->advance(lexer, false);
    }
    return is_prose_line(line, len, in_list);
}

/// Whether an unindented `line` opens a list item: the list part of
/// Asciidoctor's AnyListRx (`-`, `*`…, `.`…, `•`, `1.`, `a.`, `iv)` or a
/// callout `<1>`, then a blank), plus this grammar's own Greek marker (`α.`).
static inline bool is_list_line(i32 const *line, usize len) {
    usize i = 0;
    if(len == 0) {
        return false;
    }
    if(is_geek_lower(line[0]) && len > 2 && line[1] == '.' && is_white_space(line[2])) {
        return true;
    }
    if(line[0] == '-' || line[0] == 0x2022) {
        i = 1;
    } else if(line[0] == '*' || line[0] == '.') {
        while(i < len && line[i] == line[0]) {
            i++;
        }
    } else if(line[0] == '<') {
        i = 1;
        if(i < len && line[i] == '.') {
            i++;
        } else {
            while(i < len && is_ascii_digit(line[i])) {
                i++;
            }
            if(i == 1) {
                return false;
            }
        }
        if(i >= len || line[i] != '>') {
            return false;
        }
        i++;
    } else {
        bool digits = true, roman = true;
        while(i < len && line[i] < 0x80 && is_word_start(line[i])) {
            i32 c = line[i];
            digits = digits && is_ascii_digit(c);
            roman = roman && (c == 'I' || c == 'V' || c == 'X' || c == 'i' || c == 'v' || c == 'x');
            i++;
        }
        if(i == 0 || i >= len) {
            return false;
        }
        bool letter = i == 1 && !digits;
        if(line[i] == '.' && (digits || letter)) {
            i++;
        } else if(line[i] == ')' && roman) {
            i++;
        } else {
            return false;
        }
    }
    return i < len && is_white_space(line[i]);
}

/// Whether an unindented `line` is a description list term: the description
/// list part of Asciidoctor's AnyListRx, a term followed by `::`, `:::`,
/// `::::` or `;;` and then a blank or the end of the line. A line comment is
/// not a term.
static inline bool is_dlist_line(i32 const *line, usize len) {
    if(len >= 3 && line[0] == '/' && line[1] == '/' && line[2] != '/') {
        return false;
    }
    if(len == 2 && line[0] == '/' && line[1] == '/') {
        return false;
    }
    // The delimiter needs at least one character of term before it.
    for(usize i = 2; i < len; i++) {
        i32 c = line[i];
        if((c != ':' && c != ';') || line[i - 1] != c) {
            continue;
        }
        usize j = i + 1;
        if(c == ':') {
            while(j < len && j - i < 3 && line[j] == ':') {
                j++;
            }
        }
        if(j == len || is_white_space(line[j])) {
            return true;
        }
    }
    return false;
}

/// Conservative: a line is prose only when nothing about it could open a block.
/// Asciidoctor does not end a paragraph at a list or description list line,
/// so those join the paragraph, unless the paragraph is attached to a list
/// item (`in_list`), where such a line starts the next item.
static inline bool is_prose_line(i32 const *line, usize len, bool in_list) {
    // Blank or indented.
    if(len == 0 || is_white_space(line[0])) {
        return false;
    }

    if(is_list_line(line, len) || is_dlist_line(line, len)) {
        return !in_list;
    }

    // Opening with a marker character (`*`, `.`, `[`, `=`, `|`, …).
    if(!is_word_start(line[0])) {
        return false;
    }

    // An admonition label: `NOTE:` and the like.
    static char const *const labels[] = {"NOTE", "TIP", "IMPORTANT", "CAUTION", "WARNING"};
    for(usize l = 0; l < sizeof(labels) / sizeof(labels[0]); l++) {
        usize n = strlen(labels[l]);
        if(len > n && line[n] == ':') {
            usize i = 0;
            while(i < n && line[i] == (i32)labels[l][i]) {
                i++;
            }
            if(i == n) {
                return false;
            }
        }
    }

    // What is left with a doubled `:` or `;` is not a description list term:
    // block macros (`image::x[]`), preprocessor directives (`ifdef::x[]`) and
    // prose such as `std::vector`. These still end the paragraph.
    for(usize i = 1; i < len; i++) {
        if((line[i] == ':' && line[i - 1] == ':') || (line[i] == ';' && line[i - 1] == ';')) {
            return false;
        }
    }

    return true;
}
