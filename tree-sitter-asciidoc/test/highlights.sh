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
