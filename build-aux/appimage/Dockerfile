# Ubuntu 26.04 build environment for the AppImage (bosn.toml, task `appimage`).
# The build dependencies are installed by build.py's own `deps` step, so this
# image has exactly the packages the workflow's runner installs; the task then
# runs `build.py --skip-deps` with the source bind-mounted at /src.
FROM ubuntu:26.04@sha256:f144425ff09be612d6d9ad965196e9cdc23dae1f42110a8a11a3e9a8198759f7
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl git
# uv, pinned by version and checksum (CI uses astral-sh/setup-uv).
ARG UV_VERSION=0.12.13
ARG UV_SHA256=745765a3b6e360ad76743599ae5c42e9278c7edf8bbff9fc76d05bf2623a04dd
RUN curl -fsSLo /tmp/uv.tar.gz "https://github.com/astral-sh/uv/releases/download/${UV_VERSION}/uv-x86_64-unknown-linux-gnu.tar.gz" \
 && echo "${UV_SHA256}  /tmp/uv.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/uv.tar.gz -C /usr/local/bin --strip-components=1 \
 && rm /tmp/uv.tar.gz
COPY build-aux/appimage/build.py /opt/obs-appimage/build-aux/appimage/build.py
COPY rust/tools/linux-validate/install-zccache.sh /opt/obs-appimage/rust/tools/linux-validate/install-zccache.sh
RUN cd /opt/obs-appimage && uv run build-aux/appimage/build.py --steps deps \
 && rm -rf /opt/obs-appimage
# The toolchain pinned in rust-toolchain.toml, so CMake/corrosion find it
# without fetching it during the build (the runner image ships Rust too).
ENV RUSTUP_HOME=/usr/local/rustup CARGO_HOME=/usr/local/cargo
ENV PATH=/usr/local/cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
COPY rust-toolchain.toml /tmp/rust-toolchain.toml
RUN toolchain="$(sed -n 's/^channel = "\(.*\)"/\1/p' /tmp/rust-toolchain.toml)" \
 && curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain "$toolchain" \
 && rm /tmp/rust-toolchain.toml
# The source is bind-mounted and owned by the host user.
RUN git config --system --add safe.directory '*'
