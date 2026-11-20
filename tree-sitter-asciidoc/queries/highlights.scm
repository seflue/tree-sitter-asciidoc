;; The document title node holds the whole header, author and revision
;; lines included, so only its marker and title text are the heading.
(document_title
  [
    (title_h0_marker)
    (line)
  ] @markup.heading.1)

(title1) @markup.heading.2

(title2) @markup.heading.3

(title3) @markup.heading.4

(title4) @markup.heading.5

(title5) @markup.heading.6

(email) @markup.link.url @markup.link

(author_line
  ";" @punctuation.delimiter)

(revision_line
  [
    ","
    ":"
  ] @punctuation.delimiter)

(list_continuation) @constant

[
  (firstname)
  (middlename)
  (lastname)
] @attribute

(revnumber) @number

(revdate) @string.special

(revremark) @string

[
  (table_block_marker)
  (csv_table_block_marker)
  (dsv_table_block_marker)
] @punctuation.special

(table_cell_attr) @attribute

(table_cell
  "|" @punctuation.special)

(csv_record
  "," @punctuation.special)

(dsv_record
  ":" @punctuation.special)

[
  (breaks)
  (hard_wrap)
  (quoted_block_md_marker)
  (quoted_paragraph_marker)
  (open_block_marker)
  (listing_block_start_marker)
  (listing_block_end_marker)
  (literal_block_marker)
  (passthrough_block_marker)
  (quoted_block_start_marker)
  (quoted_block_end_marker)
  (sidebar_block_start_marker)
  (sidebar_block_end_marker)
  (ntable_block_marker)
  (callout_marker)
] @punctuation.special

(ntable_cell
  "!" @punctuation.special)

(checked_list_marker_unchecked) @markup.list.unchecked

(checked_list_marker_checked) @markup.list.checked

[
  (list_marker_star)
  (list_marker_hyphen)
  (list_marker_dot)
  (list_marker_digit)
  (list_marker_geek)
  (list_marker_alpha)
] @markup.list

(description_marker) @markup.list

(description_list_item
  (term) @markup.strong)

[
  (line_comment)
  (block_comment)
] @comment @spell

[
  (document_attr_marker)
  (element_attr_marker)
  (block_anchor_marker)
] @punctuation.delimiter

(document_attr
  (attr_name) @property)

(positional_attr) @attribute
(block_style) @type
(id) @label
(block_anchor
  (reftext) @markup.link.label)
(role) @attribute
(option) @attribute

(block_title
  (block_title_marker) @punctuation.special) @attribute

(ident_block) @markup.raw.block

(literal_block
  (literal_block_body) @markup.raw.block)

(callout_list_marker) @punctuation.special

(block_macro
  (block_macro_name) @keyword
  "::" @punctuation.delimiter
  (target)? @markup.link
  "[" @punctuation.bracket
  "]" @punctuation.bracket)

(attribute_name) @attribute

(attribute_value) @variable.parameter

(admonition
  (admonition_important) @comment.error
  ":" @comment.error)

(admonition
  (admonition_warning) @comment.warning
  ":" @comment.warning)

(admonition
  (admonition_caution) @comment.warning
  ":" @comment.warning)

(admonition
  (admonition_note) @comment.note
  ":" @comment.note)

(admonition
  (admonition_tip) @comment.note
  ":" @comment.note)

((section_block
  (element_attr
    (element_attr_marker) @comment.note
    (positional_attr (block_style) @_style @comment.note)
    (element_attr_marker) @comment.note)
  [(paragraph) (delimited_block) (open_block)])
  (#any-of? @_style "NOTE" "TIP"))

((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (delimited_block_start_marker) @comment.note
     (delimited_block_end_marker) @comment.note)
   (open_block
     (open_block_marker) @comment.note
     (open_block_marker) @comment.note)])
  (#match? @_style "^(NOTE|TIP)([#.%]|$)"))

; Every paragraph directly in the body, whatever stands between them.
; A styled paragraph in the body gets the colour too (rare, accepted).
((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (section_block
       (paragraph) @comment.note))
   (open_block
     (section_block
       (paragraph) @comment.note))])
  (#match? @_style "^(NOTE|TIP)([#.%]|$)"))

((section_block
  (element_attr
    (element_attr_marker) @comment.warning
    (positional_attr (block_style) @_style @comment.warning)
    (element_attr_marker) @comment.warning)
  [(paragraph) (delimited_block) (open_block)])
  (#any-of? @_style "CAUTION" "WARNING"))

((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (delimited_block_start_marker) @comment.warning
     (delimited_block_end_marker) @comment.warning)
   (open_block
     (open_block_marker) @comment.warning
     (open_block_marker) @comment.warning)])
  (#match? @_style "^(CAUTION|WARNING)([#.%]|$)"))

; Every paragraph directly in the body, whatever stands between them.
; A styled paragraph in the body gets the colour too (rare, accepted).
((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (section_block
       (paragraph) @comment.warning))
   (open_block
     (section_block
       (paragraph) @comment.warning))])
  (#match? @_style "^(CAUTION|WARNING)([#.%]|$)"))

((section_block
  (element_attr
    (element_attr_marker) @comment.error
    (positional_attr (block_style) @_style @comment.error)
    (element_attr_marker) @comment.error)
  [(paragraph) (delimited_block) (open_block)])
  (#eq? @_style "IMPORTANT"))

((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (delimited_block_start_marker) @comment.error
     (delimited_block_end_marker) @comment.error)
   (open_block
     (open_block_marker) @comment.error
     (open_block_marker) @comment.error)])
  (#match? @_style "^IMPORTANT([#.%]|$)"))

; Every paragraph directly in the body, whatever stands between them.
; A styled paragraph in the body gets the colour too (rare, accepted).
((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  [(delimited_block
     (section_block
       (paragraph) @comment.error))
   (open_block
     (section_block
       (paragraph) @comment.error))])
  (#match? @_style "^IMPORTANT([#.%]|$)"))

((section_block
  (element_attr
    (element_attr_marker)
    .
    (positional_attr) @_style)
  (listing_block
    (listing_block_body) @markup.raw.block))
  ; Same diagram list as injections.scm; shorthands (`plantuml#arch`) count.
  (#not-match? @_style
    "^(a2s|barcode|blockdiag|bpmn|bytefield|d2|dbml|diagrams|ditaa|dpic|erd|gnuplot|graphviz|lilypond|meme|mermaid|msc|nomnoml|pikchr|plantuml|shaape|smcat|structurizr|svgbob|symbolator|syntrax|tikz|umlet|vega|wavedrom)([#.%]|$)"))

; A listing without a style is plain raw text: no attribute list, or one
; that leads with a named attribute (`[subs=x]`). Shorthand-only lists
; (`[#id]`) fall under the styled pattern above.
(section_block
  .
  [
    (block_title)
    (block_anchor)
  ]*
  .
  (listing_block
    (listing_block_body) @markup.raw.block))

(section_block
  (element_attr
    (element_attr_marker)
    .
    (named_attr))
  (listing_block
    (listing_block_body) @markup.raw.block))

(line) @spell
