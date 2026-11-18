#!/bin/sh
set -eu

fixture=$(mktemp)
trap 'rm -f "$fixture"' EXIT
printf '%s\n' '<https://example.org> and 1 < 2 > 0' >"$fixture"

# The angle brackets around an autolink are punctuation, the URL between
# them is the link; a `<` or `>` in prose stays unstyled.
output=$(tree-sitter query queries/highlights.scm "$fixture" --captures 2>/dev/null)
bracket_count=$(printf '%s\n' "$output" | awk '/capture: [0-9]+ - punctuation.bracket, start: \(0, (0|20)\),/{count++} END{print count+0}')
other_brackets=$(printf '%s\n' "$output" | awk '/punctuation.bracket/{count++} END{print count+0}')
url_count=$(printf '%s\n' "$output" | awk '/capture: [0-9]+ - markup.link.url,.*text: `https:\/\/example.org`$/{count++} END{print count+0}')

if [ "$bracket_count" -ne 2 ] || [ "$other_brackets" -ne 2 ] || [ "$url_count" -ne 1 ]; then
  printf '%s\n' "$output"
  exit 1
fi
