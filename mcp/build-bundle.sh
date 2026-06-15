#!/usr/bin/env bash
# Build the imgcli-mcp MCP bundle (.mcpb) for publishing on Smithery.
#
# The bundle is a self-contained zip: the compiled stdio server (dist/) plus its
# *production* node_modules and manifest.json, so it runs offline without an
# npm install. The imgcli C binary is NOT bundled (it is platform-specific) — it
# stays a prerequisite (brew install swperb/tap/imgcli) configured via IMGCLI_BIN.
#
# WORKAROUND — https://github.com/smithery-ai/cli/issues/787
# MCPB v0.3 forbids a per-tool `inputSchema` (`mcpb pack` rejects it), but
# Smithery's registry REQUIRES one. So a `mcpb pack` bundle lists zero
# capabilities, and a bundle that declares tools can't be packed. To get
# capabilities onto the Smithery listing we inject the tool schemas and zip the
# bundle directly — Smithery's own bundle reader is lenient and accepts them.
# The result is intentionally a non-conformant MCPB, which only matters for a
# *direct* install into a strict host (e.g. Claude Desktop); this artifact is
# never used that way (that audience uses `npx imgcli-mcp`). When #787 ships,
# delete the injection below and just `mcpb pack` a manifest that carries tools.
#
# MCPB_STRICT=1 instead produces a spec-conformant bundle via `mcpb pack` (no
# tools → no capabilities on Smithery).
#
# Usage: cd mcp && ./build-bundle.sh   ->  mcp/imgcli-mcp.mcpb
set -euo pipefail

cd "$(dirname "$0")"                       # the mcp/ directory
OUT="$PWD/imgcli-mcp.mcpb"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> building the TypeScript server"
npm run build >/dev/null

echo "==> staging a production-only tree"
cp -R dist "$STAGE/dist"
cp manifest.json package.json README.md "$STAGE/"
cp ../docs/img/icon.png "$STAGE/icon.png"      # bundle icon (referenced by manifest)
( cd "$STAGE" && npm install --omit=dev --ignore-scripts --no-audit --no-fund --silent )

if [ "${MCPB_STRICT:-0}" = "1" ]; then
  echo "==> packing a spec-conformant bundle (mcpb pack, no tools)"
  npx -y @anthropic-ai/mcpb pack "$STAGE" "$OUT"
else
  echo "==> injecting tool schemas for the Smithery listing (workaround #787)"
  python3 - "$STAGE/manifest.json" <<'PY'
import json, sys
p = sys.argv[1]
m = json.load(open(p))
m["tools"] = [
    {
        "name": "convert_image",
        "description": "Convert, resize, crop, rotate, filter, or composite an image (output format from the file extension; optional ffmpeg-style filtergraph).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "input": {"type": "string", "description": "Path to the input image file (or a generator like 'testsrc=640x480')."},
                "output": {"type": "string", "description": "Path to write; the extension picks the format (png/jpg/bmp/tga/ppm/qoi)."},
                "filters": {"type": "string", "description": "Filtergraph, e.g. \"scale=512:-1,grayscale\". Omit for a plain conversion."},
                "quality": {"type": "integer", "minimum": 1, "maximum": 100, "description": "JPEG quality 1-100 (default 90; ignored for non-JPEG)."},
                "overwrite": {"type": "boolean", "description": "Overwrite the output if it exists (default true)."},
            },
            "required": ["input", "output"],
        },
    },
    {
        "name": "probe_image",
        "description": "Return an image's width, height, and channel count without converting it.",
        "inputSchema": {"type": "object", "properties": {"input": {"type": "string", "description": "Path to the input image file."}}, "required": ["input"]},
    },
    {
        "name": "list_filters",
        "description": "List the full imgcli filter catalogue.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]
json.dump(m, open(p, "w"), indent=2)
print("    tools: " + ", ".join(t["name"] for t in m["tools"]))
PY
  echo "==> packing the bundle (direct zip so the tool schemas survive)"
  ( cd "$STAGE" && rm -f out.mcpb && zip -r -X -q out.mcpb . -x '*.DS_Store' && mv out.mcpb "$OUT" )
fi

echo "==> done: $OUT"
