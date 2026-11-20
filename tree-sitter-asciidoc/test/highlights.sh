#!/bin/sh
set -eu

fixture=$(mktemp)
trap 'rm -f "$fixture"' EXIT
cat >"$fixture" <<'EOF'
= Title
Ada Lovelace <ada@example.org>
v1.0, 2026-01-01: Draft
EOF

# The heading of the document title ends with the title line: the author
# and revision lines below it, gaps included, are no heading.
output=$(tree-sitter query queries/highlights.scm "$fixture" --captures 2>/dev/null)
heading_count=$(printf '%s\n' "$output" | awk '/capture: [0-9]+ - markup.heading.1,/{count++} END{print count+0}')
past_title=$(printf '%s\n' "$output" | awk '
/capture: [0-9]+ - markup.heading.1,/{
  match($0, /end: \([0-9]+, [0-9]+\)/)
  split(substr($0, RSTART + 6, RLENGTH - 7), pos, ", ")
  if(pos[1] > 1 || (pos[1] == 1 && pos[2] > 0)) count++
} END{print count+0}')

if [ "$heading_count" -eq 0 ] || [ "$past_title" -ne 0 ]; then
  printf '%s\n' "$output"
  exit 1
fi

# captures NAME LINE FIXTURE-TEXT: succeeds when a capture with that name
# starts on the line (0-based) of the fixture.
captures_of() {
  printf '%s\n' "$1" >"$fixture"
  tree-sitter query queries/highlights.scm "$fixture" --captures 2>/dev/null
}
has_capture() {
  printf '%s\n' "$1" | grep -q "capture: [0-9]* - $2, start: ($3, "
}
fail() {
  printf '%s\n' "$1"
  exit 1
}

# A listing without a style has a raw body, the content stays unstyled.
output=$(captures_of '----
*x*
----')
has_capture "$output" 'markup.raw.block' 1 || fail "$output"

# In [source,rust] the style is a type, not an attribute: the attribute
# capture of the whole positional attribute must come first.
output=$(captures_of '[source,rust]
----
fn x() {}
----')
last=$(printf '%s\n' "$output" | grep 'start: (0, 1), end: (0, 7)' | grep -v ' - _' | tail -n 1)
case "$last" in
*' - type,'*) ;;
*) fail "$output" ;;
esac

# [NOTE] with a ==== block: delimiters and the body carry a capture.
output=$(captures_of '[NOTE]
====
text
====')
has_capture "$output" 'comment.note' 1 || fail "$output"
has_capture "$output" 'comment.note' 2 || fail "$output"
has_capture "$output" 'comment.note' 3 || fail "$output"

# A diagram block keeps no raw body (its language is injected), and a
# later positional attribute does not make it one.
output=$(captures_of '[plantuml,x]
----
b
----')
if has_capture "$output" 'markup.raw.block' 2; then
  fail "$output"
fi

# A shorthand after the admonition style keeps the block marked.
output=$(captures_of '[NOTE.role]
====
text
====')
has_capture "$output" 'comment.note' 1 || fail "$output"
has_capture "$output" 'comment.note' 2 || fail "$output"
has_capture "$output" 'comment.note' 3 || fail "$output"

# A shorthand after a diagram style does not make the body raw.
output=$(captures_of '[plantuml#arch]
----
b
----')
if has_capture "$output" 'markup.raw.block' 2; then
  fail "$output"
fi

# A listing with named attributes only has no style, so a raw body.
output=$(captures_of '[subs=+quotes]
----
*x*
----')
has_capture "$output" 'markup.raw.block' 2 || fail "$output"

# So has a listing with shorthands only.
output=$(captures_of '[#id.role]
----
*x*
----')
has_capture "$output" 'markup.raw.block' 2 || fail "$output"

# Every paragraph in a [NOTE] block carries the capture as a whole (one
# capture per paragraph, not per line, which keeps the first line of a
# multi-line paragraph styled under the inline injection).
output=$(captures_of '[NOTE]
====
a1
a2

b1
b2
====')
for line in 2 5; do
  has_capture "$output" 'comment.note' "$line" || fail "$output"
done

# A titled listing without a style is plain raw text.
output=$(captures_of '.Title
----
*x*
----')
has_capture "$output" 'markup.raw.block' 2 || fail "$output"

# A diagram style before a title keeps the body from being raw.
output=$(captures_of '[plantuml]
.Title
----
b
----')
if has_capture "$output" 'markup.raw.block' 3; then
  fail "$output"
fi

# A block anchor above an unstyled listing keeps the body raw.
output=$(captures_of '[[lid]]
----
*w*
----')
has_capture "$output" 'markup.raw.block' 2 || fail "$output"

# So does a listing in a list continuation.
output=$(captures_of '* item
+
----
*v*
----')
has_capture "$output" 'markup.raw.block' 3 || fail "$output"

# A paragraph after a list or a listing in the body of a [NOTE] block is
# still part of the admonition, one capture per direct paragraph.
output=$(captures_of '[NOTE]
====
One

* item

Two

----
code
----

Three
====')
for line in 2 6 12; do
  has_capture "$output" 'comment.note' "$line" || fail "$output"
done

# A paragraph of a nested block in the body is no direct body paragraph.
output=$(captures_of '[WARNING]
====
One

****
side
****

Two
====')
has_capture "$output" 'comment.warning' 2 || fail "$output"
has_capture "$output" 'comment.warning' 8 || fail "$output"
if has_capture "$output" 'comment.warning' 5; then
  fail "$output"
fi

# Asciidoctor makes an admonition only from a paragraph, an example block
# or an open block: the style line over other blocks gets no capture.
for block in '----
x
----' '****
x
****' '* x' '....
x
....' '|===
|x
|==='; do
  output=$(captures_of "[NOTE]
$block")
  if has_capture "$output" 'comment.note' 0; then
    fail "$output"
  fi
  # Neither the markers nor a paragraph of the body.
  if printf '%s\n' "$output" | grep -q 'comment.note, start: ([1-9]'; then
    fail "$output"
  fi
done

# Over a paragraph, an example block and an open block it still is one.
output=$(captures_of '[TIP]
text')
has_capture "$output" 'comment.note' 0 || fail "$output"
output=$(captures_of '[IMPORTANT]
====
x
====')
has_capture "$output" 'comment.error' 0 || fail "$output"
output=$(captures_of '[CAUTION]
--
x
--')
has_capture "$output" 'comment.warning' 0 || fail "$output"

# An open block is an admonition like an example block: markers and every
# direct body paragraph carry the capture, a nested block's paragraph not.
output=$(captures_of '[NOTE]
--
a

* item

b

****
side
****
--')
for line in 1 2 6 11; do
  has_capture "$output" 'comment.note' "$line" || fail "$output"
done
if has_capture "$output" 'comment.note' 9; then
  fail "$output"
fi

# A paragraph in a list continuation inside the body is not a direct one.
output=$(captures_of '[NOTE]
====
One

* item
+
para
====')
has_capture "$output" 'comment.note' 2 || fail "$output"
if has_capture "$output" 'comment.note' 6; then
  fail "$output"
fi

# A title line in the body stays plain, its paragraph is coloured.
output=$(captures_of '[NOTE]
====
.Title
text
====')
if has_capture "$output" 'comment.note' 2; then
  fail "$output"
fi
has_capture "$output" 'comment.note' 3 || fail "$output"

# A shorthand after the style over a plain paragraph.
output=$(captures_of '[NOTE.role]
text')
has_capture "$output" 'comment.note' 0 || fail "$output"
