# Linux build environment for Tier 2 validation (docs/rust-port/testing-policy.md).
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
COPY apt-packages.txt /tmp/apt-packages.txt
RUN apt-get update && grep -v '^#' /tmp/apt-packages.txt | xargs apt-get install -y --no-install-recommends \
 && if apt-cache show libvpl-dev >/dev/null 2>&1; then apt-get install -y --no-install-recommends libvpl-dev; fi
# libvpl-dev is amd64-only. validate.sh leaves QSV off, so an arm64 image skips it.
# Install exactly the toolchain pinned in rust-toolchain.toml (run.sh passes it)
# so corrosion/CMake finds it; rustup must not have to fetch it at run time.
# Compiler launcher for the CMake builds: zccache, pinned with its checksum.
COPY install-zccache.sh /tmp/install-zccache.sh
RUN /tmp/install-zccache.sh /usr/local/bin && rm /tmp/install-zccache.sh
ARG RUST_TOOLCHAIN
RUN test -n "$RUST_TOOLCHAIN" \
 && curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain "$RUST_TOOLCHAIN" -c rustfmt -c clippy
ENV PATH=/root/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
