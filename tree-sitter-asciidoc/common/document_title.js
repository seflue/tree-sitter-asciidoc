const { anySep1 } = require('../../common/common');

// A character of the text before the first `,` or `:` of a revision line.
const REVISION_CH = /[^,:\r\n]/;

exports.rules = {
  document_title: $ =>
    seq(
      $.title_h0_marker,
      $._WHITE_SPACE,
      $.line,
      choice(
        seq(
          $.author_line,
          $._NEWLINE,
          repeat($._header_comment),
          choice(
            // A `:` line that is no attribute entry (`:to`, an entry still
            // being typed) is no revision line: the scanner ends the header
            // in front of it, after the author line or any comment, and the
            // line stays with the body.
            $._header_end,
            seq(
              optional(choice($.revision_line, $._header_entry)),
              repeat(choice($.document_attr, $.block_macro)),
              $._block_end,
            ),
          ),
        ),
        seq(repeat(choice($.document_attr, $.block_macro)), $._block_end),
      ),
    ),
  // A comment line ends with its newline; a comment block does not, and
  // without the explicit newline the title would end there.
  _header_comment: $ => choice($.line_comment, seq($.block_comment, $._block_end)),
  author_line: $ => anySep1($.author, seq(';', $._WHITE_SPACE)),
  // A single name is an author too (Asciidoctor's AuthorInfoLineRx makes
  // the last name optional).
  author: $ =>
    seq(
      alias($._author_line_word, $.firstname),
      optional(
        seq(
          $._WHITE_SPACE,
          optional(seq(alias($._author_line_word, $.middlename), $._WHITE_SPACE)),
          alias($._author_line_word, $.lastname),
          optional(seq($._WHITE_SPACE, '<', $.email, '>')),
        ),
      ),
    ),
  // One token, so the newline extra cannot slip into a name and carry the
  // author line over the following lines. A name never starts with `<`,
  // which opens the email.
  _author_line_word: $ =>
    token(
      seq(
        choice(/[^ ;<\r\n]/, '\\ ', '\\;'),
        repeat(choice(/[^ ;\r\n]/, '\\ ', '\\;')),
      ),
    ),
  email: $ =>
    // biome-ignore lint/suspicious/noControlCharactersInRegex: xxx
    /(?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\.){3}(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\])/,
  // The line after the author line is the revision line (RevisionInfoLineRx).
  // Asciidoctor processes comments and attribute entries before it reads
  // that line; the grammar takes comments only (an entry there ends the
  // slot: `:toc: left` then `v1.0` makes `v1.0` a body paragraph here, and a
  // revnumber in Asciidoctor). With a comma, the text before it is the
  // revnumber (possibly empty) and the text after it the date. Without one,
  // the line is the revnumber when it starts with `v` and the date
  // otherwise. `:` opens the remark. A line that starts with `:` is no
  // revision line here, which deviates from Asciidoctor for `:a, b` (it
  // reads that as revision line with the date `b`); without a comma it
  // agrees, and Asciidoctor gives the line back to the body. There is no
  // escape: `\,` and `\:` split like `,` and `:`.
  revision_line: $ =>
    choice(
      seq(
        optional(alias(choice($._revision_v_word, $._revision_other_word), $.revnumber)),
        ',',
        optional($._WHITE_SPACE),
        optional(alias($._revision_date, $.revdate)),
        optional($._revision_remark),
      ),
      seq(alias($._revision_v_word, $.revnumber), optional($._revision_remark)),
      seq(alias($._revision_other_word, $.revdate), optional($._revision_remark)),
    ),
  // The `:` must follow the text directly: the newline is an extra, so a
  // plain `:` would take the `:` that opens the attribute entry on the next
  // line. The remark (and the space before it) stay on the same line for the
  // same reason, so they are immediate tokens.
  _revision_remark: $ =>
    seq(
      token.immediate(':'),
      optional(token.immediate(/[ \t]+/)),
      optional(alias(token.immediate(/[^ \t\r\n][^\r\n]*/), $.revremark)),
    ),
  // The text up to the first `,` or `:`, as one token each for the two
  // readings of a line without comma: a version when it starts with `v`,
  // a date otherwise. Both stay tokens (not character runs), so the first
  // character does not commit the parser before the comma shows.
  _revision_v_word: $ => token(seq('v', repeat(REVISION_CH))),
  _revision_other_word: $ => token(seq(/[^v,:\r\n]/, repeat(REVISION_CH))),
  // The date after a comma may hold further commas, which the two words
  // above cannot.
  _revision_date: $ => token(seq(/[^ \t:\r\n]/, repeat(/[^:\r\n]/))),
  document_attr: $ =>
    seq(
      $.document_attr_marker,
      // One token, so the newline extra cannot slip into the name. The
      // scanner has already checked that the closing `:` is on this line.
      alias(token.immediate(/[^:\r\n]+/), $.attr_name),
      alias(':', $.document_attr_marker),
      // The value may be absent after the separating space (`:sourceFile: `),
      // so the line itself is optional too.
      optional(seq(token.immediate(/[ \t]/), optional(alias($.escaped_line, $.line)))),
      $._block_end,
    ),
};
