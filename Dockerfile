# Self-contained image for the imgcli MCP server (imgcli-mcp).
#
# Builds the imgcli C binary AND the Node MCP server into one image. The server
# picks its transport from the environment:
#   - no PORT  -> stdio  (Glama's listing check, local `docker run -i`)
#   - PORT set -> Streamable HTTP at /mcp on that port (Smithery's container
#                 runtime sets PORT=8081)
#
#   docker build -t imgcli-mcp .
#   docker run --rm -i imgcli-mcp                       # MCP over stdio
#   docker run --rm -e PORT=8081 -p 8081:8081 imgcli-mcp  # MCP over HTTP at :8081/mcp

# ---- build stage: compile imgcli + build the TypeScript server ----
FROM node:20-bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make                                   # -> ./imgcli (hardened C build)
WORKDIR /src/mcp
RUN npm ci && npm run build                # -> dist/ (tsc)
RUN npm prune --omit=dev                   # drop devDeps (typescript) for runtime

# ---- runtime stage: minimal, non-root ----
FROM node:20-bookworm-slim
WORKDIR /app
COPY --from=build /src/imgcli           /usr/local/bin/imgcli
COPY --from=build /src/mcp/dist         ./dist
COPY --from=build /src/mcp/node_modules ./node_modules
COPY --from=build /src/mcp/package.json ./package.json
ENV IMGCLI_BIN=/usr/local/bin/imgcli
# Documents the HTTP port; Smithery sets PORT=8081 at launch to select HTTP mode.
# Left unset by default so plain `docker run -i` still speaks stdio.
EXPOSE 8081
USER node
ENTRYPOINT ["node", "/app/dist/index.js"]
