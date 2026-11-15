# tree-sitter-asciidoc

[tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for [AsciiDoc](https://docs.asciidoctor.org/asciidoc/latest/).

## About this fork

This is a fork of [cathaysia/tree-sitter-asciidoc](https://github.com/cathaysia/tree-sitter-asciidoc), which was archived and made read-only in October 2026, and has been maintained here since then. It also takes in changes from [bovinemagnet/tree-sitter-asciidoc](https://github.com/bovinemagnet/tree-sitter-asciidoc), which continues cathaysia's grammar on its `master` branch.

Two projects use it:

- [asciidoc-lsp](https://github.com/seflue/asciidoc-lsp), an AsciiDoc language server, which includes the grammar as a git submodule pinned to a commit
- [asciidoc.nvim](https://github.com/seflue/asciidoc.nvim), the Neovim plugin, which pins a commit for nvim-treesitter

On top of bovinemagnet's `master`, this fork changes how some constructs parse, to follow Asciidoctor more closely ([compare](https://github.com/bovinemagnet/tree-sitter-asciidoc/compare/master...seflue:tree-sitter-asciidoc:main)).

The crates.io packages are cathaysia's releases and include none of bovinemagnet's or this fork's changes. This fork has no releases of its own: to use it, depend on this repository at a commit on `main`.
