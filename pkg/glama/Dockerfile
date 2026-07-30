# Glama MCP directory (glama.ai) check image — NOT required to run the tool.
#
# project-mind-mcp needs NO Docker to run. This image exists only so Glama
# can build a sandbox, launch the stdio MCP server, and run its introspection
# checks, which power the directory's score badge. The same image is exercised
# locally and in CI by pkg/glama/verify.sh to guard against drift.
#
# We pull the "-portable" release asset, which is fully statically linked
# (gcc -static) — unlike the standard asset, which dynamically links glibc/
# libstdc++ and would fail on an older base. Because it's static, the runtime
# base image is arbitrary. TARGETARCH is amd64 / arm64, matching the asset names.

FROM debian:bookworm-slim AS fetch
ARG TARGETARCH
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && curl -fsSL "https://github.com/usman8786/project-mind-mcp/releases/latest/download/project-mind-mcp-linux-${TARGETARCH}-portable.tar.gz" \
      | tar -xz -C /tmp project-mind-mcp LICENSE THIRD_PARTY_NOTICES.md \
 && chmod +x /tmp/project-mind-mcp \
 && rm -rf /var/lib/apt/lists/*

FROM debian:bookworm-slim
COPY --from=fetch /tmp/project-mind-mcp /usr/local/bin/project-mind-mcp
COPY --from=fetch /tmp/LICENSE /tmp/THIRD_PARTY_NOTICES.md /usr/share/doc/project-mind-mcp/
ENV PMM_CACHE_DIR=/tmp/pmm
ENTRYPOINT ["project-mind-mcp"]
