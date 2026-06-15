# Minimal image for the imgcli CLI itself (published to ghcr.io/swperb/imgcli).
# This is the image tool, not the MCP server — for that see the repo-root Dockerfile.
#
#   docker run --rm -v "$PWD:/work" -w /work ghcr.io/swperb/imgcli -y -i in.png out.jpg

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make                                   # -> ./imgcli (hardened C build)

FROM debian:bookworm-slim
COPY --from=build /src/imgcli /usr/local/bin/imgcli
WORKDIR /work
ENTRYPOINT ["imgcli"]
