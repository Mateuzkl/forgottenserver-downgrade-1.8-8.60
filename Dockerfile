FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl zip unzip tar build-essential cmake ninja-build pkg-config \
    ca-certificates && rm -rf /var/lib/apt/lists/*
# Instalar vcpkg
WORKDIR /opt
RUN git clone https://github.com/microsoft/vcpkg.git && cd vcpkg && ./bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"
# Instalar dependencias del proyecto
WORKDIR /usr/src/forgottenserver-downgrade
COPY vcpkg.json vcpkg-configuration.json ./
COPY vcpkg-overlays ./vcpkg-overlays/
# Pre-seed Lua's distfile to avoid transient vcpkg download timeouts in CI.
RUN set -eux; \
    mkdir -p /opt/vcpkg/downloads; \
    curl -fL --retry 10 --retry-all-errors --retry-delay 5 --connect-timeout 30 --max-time 600 \
        -o /opt/vcpkg/downloads/lua-5.5.0.tar.gz \
        https://www.lua.org/ftp/lua-5.5.0.tar.gz; \
    /opt/vcpkg/vcpkg install --triplet x64-linux
# Copiar el resto del código
COPY cmake /usr/src/forgottenserver-downgrade/cmake/
COPY src /usr/src/forgottenserver-downgrade/src/
COPY CMakeLists.txt /usr/src/forgottenserver-downgrade/
WORKDIR /usr/src/forgottenserver-downgrade
# Usar el flujo clásico de CMake con vcpkg toolchain
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build --config RelWithDebInfo

# Must track the build stage above. The binary is linked against that image's glibc,
# so an older runtime base fails to start regardless of which libraries are installed.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
RUN groupadd -r tfs && useradd -r -g tfs -d /srv -s /bin/sh tfs
COPY --from=build /usr/src/forgottenserver-downgrade/build/tfs /bin/tfs
# Fail here rather than publishing an image that exits 127 on first run.
# Written to fail closed: the shell has no pipefail, so `ldd ... | grep -q` would
# report success whenever ldd itself failed, which is exactly when the check
# matters most. Capture the output, judge ldd's own exit status first, and only
# then look for missing libraries.
RUN set -eu; \
    if ! command -v ldd >/dev/null 2>&1; then \
        echo 'ldd is unavailable in the runtime stage; cannot verify linkage' >&2; \
        exit 1; \
    fi; \
    if linkage="$(ldd /bin/tfs 2>&1)"; then \
        if printf '%s\n' "$linkage" | grep -q 'not found'; then \
            echo 'Runtime stage is missing shared libraries:' >&2; \
            printf '%s\n' "$linkage" | grep 'not found' >&2; \
            exit 1; \
        fi; \
    elif printf '%s\n' "$linkage" | grep -q 'not a dynamic executable'; then \
        echo 'Binary is statically linked; no shared libraries to verify'; \
    else \
        echo 'ldd failed on /bin/tfs:' >&2; \
        printf '%s\n' "$linkage" >&2; \
        exit 1; \
    fi
COPY data /srv/data/
COPY LICENSE README.md *.dist *.sql key.pem /srv/
RUN chown -R tfs:tfs /bin/tfs /srv
EXPOSE 7171 7172
WORKDIR /srv
VOLUME /srv
USER tfs
ENTRYPOINT ["/bin/tfs"]
