# imgcli-mcp

An [MCP](https://modelcontextprotocol.io) server that exposes
[imgcli](https://github.com/swperb/imgcli) as native tools for AI agents, so a
model can convert and process images by calling tools instead of shelling out.

## Tools

| Tool | Purpose |
| --- | --- |
| `convert_image` | Convert / resize / crop / rotate / filter / composite an image. Args: `input`, `output`, optional `filters` (ffmpeg-style chain), `quality`, `overwrite`. Returns imgcli's JSON result. |
| `probe_image` | Return an image's width/height/channels without converting. |
| `list_filters` | List the full filter catalogue. |

All arguments are passed to imgcli as an argv array (no shell), so there is no
shell-injection surface.

## Prerequisites

The `imgcli` binary must be installed:

```sh
brew install swperb/tap/imgcli      # or build from source: make && sudo make install
```

If it isn't on `PATH`, set `IMGCLI_BIN` to its absolute path.

## Transports

The server picks its transport from the environment, so the same binary works
locally and when hosted:

- **stdio** (default) — for `npx`, Claude Desktop, Cursor, and the Docker stdio
  check. Used whenever `PORT` is not set.
- **Streamable HTTP** — served at `/mcp` when `PORT` is set. This is what hosted
  platforms such as [Smithery](#deploy-on-smithery) use. The HTTP server is
  stateless (one MCP server per request), so there is no cross-caller state.

## Run

```sh
npm install      # builds dist/ via the prepare script
npm start        # stdio MCP server
# or, once published to npm:
npx imgcli-mcp

# HTTP mode (what Smithery uses): set PORT, then POST to /mcp
PORT=8081 npm start
```

## Docker

A root [`Dockerfile`](../Dockerfile) builds the `imgcli` binary and this server
into one self-contained image (no host install of imgcli needed):

```sh
docker build -t imgcli-mcp .                          # from the repo root
docker run --rm -i imgcli-mcp                         # MCP over stdio
docker run --rm -e PORT=8081 -p 8081:8081 imgcli-mcp  # MCP over HTTP at :8081/mcp
```

The stdio form is also what listing directories (e.g. Glama) use to verify the
server starts and responds to MCP introspection.

## Deploy on Smithery

[Smithery](https://smithery.ai) builds the repo's [`Dockerfile`](../Dockerfile)
and connects over Streamable HTTP, configured by
[`smithery.yaml`](../smithery.yaml) (`runtime: container`, `type: http`). Because
the container sets `PORT`, the server starts in HTTP mode automatically and the
bundled `imgcli` binary means **no configuration is required**.

To (re)deploy: push to `main`, then on Smithery open the connected repo →
**Deployments** → **Create Deployment**.

## Configure in a client

Claude Desktop / Cursor / any MCP client (`mcpServers` config):

```json
{
  "mcpServers": {
    "imgcli": {
      "command": "npx",
      "args": ["-y", "imgcli-mcp"],
      "env": { "IMGCLI_BIN": "imgcli" }
    }
  }
}
```

Or point `command` at `node` and `args` at the built `dist/index.js` for a local checkout.

## Example calls

```jsonc
// convert_image
{ "input": "photo.jpg", "output": "thumb.png", "filters": "scale=256:-1,grayscale" }
// -> {"ok":true,"output":"thumb.png","width":256,"height":171,"format":"png","bytes":34122}

// probe_image
{ "input": "photo.jpg" }
// -> {"ok":true,"inputs":[{"path":"photo.jpg","width":4000,"height":3000,"channels":4}]}
```

## Publishing

Two stages — **npm first**, then the **MCP registry** (the registry verifies the
npm package's `mcpName` field against the server `name`).

```sh
# 1. Publish to npm (needs `npm login`). Package is public via publishConfig.
cd mcp
npm publish

# 2. Publish metadata to the MCP registry
brew install mcp-publisher          # (or download from the registry's GitHub releases)
mcp-publisher login github          # device-flow auth as the io.github.swperb owner
mcp-publisher validate              # optional: check server.json
mcp-publisher publish               # publishes server.json

# verify
curl "https://registry.modelcontextprotocol.io/v0.1/servers?search=io.github.swperb/imgcli"
```

Ownership is proven by `mcpName` in `package.json` matching `name` in
`server.json` (`io.github.swperb/imgcli`). On every release keep three versions
in sync: `package.json` `version`, `server.json` `version`, and the
`server.json` `packages[].version`.

> The MCP registry is in preview; data resets can occur, so be ready to
> re-publish. There is no self-service unpublish — publish a new version to update.
