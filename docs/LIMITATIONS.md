# Limitations

Known gaps and rough edges, with how each currently parses. These are not bugs
in the "wrong output for valid input" sense so much as places where the grammar
is coarser than full AsciiDoc semantics. Improvements are tracked in
[ROADMAP.md](ROADMAP.md).

## Lists do not nest

Nested list markers (`**`, `***`, `..`) produce flat sibling items, not a nested
list tree. The marker depth is captured (the marker text is `**` etc.), but a
deeper item is a sibling of the shallower one rather than a child.

```
* a
** b      parsed as two sibling items, not b nested under a
* c
```

This is the single biggest structural gap. A correct fix needs the scanner to
track a stack of marker depths; see [ROADMAP.md](ROADMAP.md).

## Hanging-indent continuations under admonitions

A hanging-indented continuation line under an admonition (or a paragraph) is
parsed as an indented literal block, so it is highlighted as raw/monospace text:

```
NOTE: Line 1
  Line 2      parsed as a separate indented literal block
```

This is upstream issue #41. The root cause is that the external scanner emits
the indented-literal marker for any whitespace-led line without knowing whether
that line starts a new block or continues the previous one, and blank lines (the
real block separator) are invisible to the grammar. A clean fix needs reliable
blank-line / line-ending tokenization; see [ROADMAP.md](ROADMAP.md).

## Paragraphs end at any line that might open a block

Consecutive lines of prose form one `paragraph`, and constrained formatting
(`*bold*`, `_italic_`, `` `mono` ``, `#highlight#`) may run over its lines. The
scanner joins a line to the paragraph above it only when it is sure the line is
prose, so a paragraph also ends before a line that starts with anything but a
letter or digit (a non-ASCII character counts as a letter), a line that starts
like an ordered list item (`a.`, `1.`, and prose such as `Mr. Smith`), a line
that starts with an admonition label (`NOTE:`), a line that contains `::` or
`;;`, and a line longer than 512 characters. Asciidoctor would keep some of
those lines in the paragraph; here they start a paragraph of their own, as every
line did before. A line that is not prose but still parses as paragraph text,
such as a lone `+` or a `[[anchor]]` on its own line, starts a paragraph that
takes in the prose below it.

A quoted paragraph is injected together with its attribution line, so a span can
run on into the attribution: in `text *b` followed by `-- Someone*`, the
emphasis covers the attribution.

Typographic quotes (``"`…`"`` and ``'`…`'``) still have to close on the line
they open.

## Sections nest only at the document level

Inside container blocks (open, sidebar, quote, delimited blocks, table cells) a
heading is a flat `section_block`, not a nested `section`. Headings in those
positions are unusual (typically discrete headings), so this is a deliberate
scope choice rather than an oversight.

## Inline substitution order

The inline grammar reads passthroughs and quoted text in one pass, while
Asciidoctor extracts passthroughs before it applies quote substitutions. So in
`**a ++b** c++` the strong span wins here and `++` stays literal, where
Asciidoctor produces `**a ` followed by the passthrough `++b** c++`. A plus
passthrough also cannot hold a `+`: `+++a++b+++` parses to an error node.

## Inline macro gating is not enforced

`kbd:`, `btn:`, and `menu:` are always parsed. In AsciiDoc they require the
`:experimental:` document attribute. A static grammar cannot easily track
document attributes, so these are accepted unconditionally. This is
over-permissive, not incorrect for documents that do enable the attribute.

## Conditional preprocessor directives are not scoped

`ifdef::`, `ifndef::`, `ifeval::`, and `endif::` parse as generic block (or
inline) macros. The content between an opening directive and its `endif` is not
grouped or conditionally included; that is a preprocessing concern outside a
syntax tree.

## A few inline replacements are missing

The em dash (`--` between words) and the in-word smart apostrophe (the `'` in
`Olaf's`) are not converted. The explicit typographic apostrophe form
(`` `' ``) is handled. Arrows, `(C)`/`(R)`/`(TM)`, and ellipsis are handled.

## Malformed input

An unterminated quoted attribute value (for example `[cols="1,2]` with no
closing quote) is malformed and parses to an error node. tree-sitter recovers on
the next edit; this only bites while such input is being typed.
