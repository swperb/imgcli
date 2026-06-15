# Self-contained image for the imgcli MCP server (imgcli-mcp).
#
# Builds the imgcli C binary AND the Node MCP server, then runs the server over
# stdio. Useful for: Glama's listing checks (the server starts and responds to
# MCP introspection), and for anyone who wants to run the server without
# installing imgcli on the host.
#
#   docker build -t imgcli-mcp .
#   docker run --rm -i imgcli-mcp        # speaks MCP over stdio

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
USER node
ENTRYPOINT ["node", "/app/dist/index.js"]
