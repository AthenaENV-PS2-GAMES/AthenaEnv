# syntax=docker/dockerfile:1
# Pinned to immutable SHA256 digest for guaranteed reproducible builds
ARG BASE_IMAGE=ghcr.io/ps2homebrew/ps2homebrew@sha256:207523ad98f17fd406afa7dd0c6cfe10730627545cbe45247f0bd73c8a66c376
FROM ${BASE_IMAGE}

# OpenContainers image metadata
LABEL org.opencontainers.image.title="AthenaEnv Build Container" \
      org.opencontainers.image.description="Minimal reproducible build environment for AthenaEnv PS2 Homebrew with QuickJS" \
      org.opencontainers.image.source="https://github.com/GibranKhalil/AthenaEnv" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.vendor="AthenaEnv"

# Set environment variables for PS2 toolchains
ENV PS2DEV=/usr/local/ps2dev
ENV PS2SDK=$PS2DEV/ps2sdk
ENV PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin

# Build arguments for non-root user (default to 1000:1000)
ARG USERNAME=developer
ARG USER_UID=1000
ARG USER_GID=1000

# Create non-root user (compatible with Alpine Linux and Debian/Ubuntu)
RUN set -e; \
    if id "${USERNAME}" >/dev/null 2>&1; then \
        echo "User ${USERNAME} already exists"; \
    elif command -v adduser >/dev/null 2>&1 && [ ! -x /usr/sbin/useradd ]; then \
        addgroup -g "${USER_GID}" "${USERNAME}" 2>/dev/null || true; \
        adduser -D -u "${USER_UID}" -G "${USERNAME}" -s "$(command -v bash || echo /bin/sh)" "${USERNAME}"; \
    else \
        groupadd -g "${USER_GID}" "${USERNAME}" 2>/dev/null || true; \
        useradd -u "${USER_UID}" -g "${USER_GID}" -m -s "$(command -v bash || echo /bin/sh)" "${USERNAME}"; \
    fi && \
    mkdir -p /src && \
    chown -R ${USER_UID}:${USER_GID} /src

WORKDIR /src

# Switch to non-root user
USER ${USERNAME}

# Default command
CMD ["make", "clean", "all"]
