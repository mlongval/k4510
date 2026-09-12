#!/bin/bash
# Render the K4510 design documents into an EPUB (for reading on the Elipsa).
set -e
cd "$(dirname "$0")"

cat > .meta.yaml <<'HDR'
---
title: "K4510"
subtitle: "A Modern C64 Successor for the Bare-Metal Raspberry Pi 3B+"
creator:
  - role: author
    text: "Design Document"
date: "2026-08-21"
lang: en
HDR
echo '---' >> .meta.yaml

pandoc .meta.yaml K4510-Design.md \
  --to=epub3 \
  --from=markdown+pipe_tables \
  --toc --toc-depth=3 \
  --split-level=2 \
  --epub-cover-image=cover.png \
  -o K4510-Design.epub

rm -f .meta.yaml
echo "Wrote K4510-Design.epub"
