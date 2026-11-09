const { anySep1, escaped_ch } = require('../../common/common');

exports.rules = {
  document_title: $ =>
    seq(
      $.title_h0_marker,
      $._WHITE_SPACE,
      $.line,
      optional(seq($.author_line, seq($._NEWLINE, optional($.revision_line)))),
      repeat(choice($.document_attr, $.block_macro)),
      $._block_end,
    ),
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
  revision_line: $ =>
    seq(
      $.revnumber,
      ',',
      $._WHITE_SPACE,
      $.revdate,
      ':',
      $._WHITE_SPACE,
      $.revremark,
    ),
  revnumber: $ => repeat1(escaped_ch(',', true)),
  revdate: $ => repeat1(escaped_ch(':', true)),
  revremark: $ => repeat1(escaped_ch('', true)),
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
