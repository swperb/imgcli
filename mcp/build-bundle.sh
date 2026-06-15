#!/usr/bin/env bash
# Build the imgcli-mcp MCP bundle (.mcpb) for distribution on Smithery and any
# other MCP host that installs local servers.
#
# The bundle is a self-contained zip: the compiled stdio server (dist/) plus its
# *production* node_modules and manifest.json, so it runs offline without an
# npm install. The imgcli C binary is NOT bundled (it is platform-specific) — it
# stays a prerequisite (brew install swperb/tap/imgcli) configured via IMGCLI_BIN.
#
# TODO: manifest.json intentionally omits a `tools` array. `mcpb pack` forbids
# per-tool `inputSchema`, but `smithery mcp publish` requires it, so declaring
# tools fails the Smithery publish with 400 "expected object, received undefined"
# (one per tool). Tracked upstream: https://github.com/smithery-ai/cli/issues/787
# Once that's fixed, re-add the tools block to manifest.json for a richer listing.
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
( cd "$STAGE" && npm install --omit=dev --ignore-scripts --no-audit --no-fund --silent )

echo "==> packing the bundle"
npx -y @anthropic-ai/mcpb pack "$STAGE" "$OUT"

echo "==> done: $OUT"
