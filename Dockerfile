FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG VCPKG_COMMIT=55fab67aea1027f7179ae6b5c54a5ba9091c16aa
ARG BUILD_JOBS=4

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        build-essential \
        ca-certificates \
        ccache \
        clang \
        curl \
        git \
        libtool \
        ninja-build \
        pkg-config \
        python3 \
        python3-venv \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir cmake==4.0.3

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout "${VCPKG_COMMIT}" \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV PATH="/opt/cmake/bin:${PATH}" \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DEFAULT_TRIPLET=x64-linux

WORKDIR /workspace
COPY . .

RUN cmake -S . -B build/container -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build/container --target disk --parallel "${BUILD_JOBS}"

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl libgcc-s1 libstdc++6 postgresql-client \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 disk \
    && useradd --uid 10001 --gid disk --create-home --home-dir /var/lib/disk --shell /usr/sbin/nologin disk

COPY --from=builder /workspace/build/container/src/disk /app/disk
COPY deploy/config.distributed.json /app/config.json
COPY scripts/migrate-db.sh /app/scripts/migrate-db.sh
COPY sql/migrations/manifest.tsv sql/migrations/*_forward.sql /app/sql/migrations/

RUN chmod 0555 /app/scripts/migrate-db.sh \
    && chown -R disk:disk /app /var/lib/disk

USER 10001:10001
WORKDIR /app
EXPOSE 8080

ENTRYPOINT ["/app/disk"]
