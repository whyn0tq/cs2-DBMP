FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

ARG PG_VERSION=17.2

RUN apt-get update \
    && apt-get install -y --no-install-recommends default-libmysqlclient-dev libssl-dev zlib1g-dev lld git python3-pip curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir git+https://github.com/alliedmodders/ambuild

RUN curl -fsSL https://ftp.postgresql.org/pub/source/v${PG_VERSION}/postgresql-${PG_VERSION}.tar.gz | tar -xz -C /tmp \
    && cd /tmp/postgresql-${PG_VERSION} \
    && CFLAGS="-O2 -fPIC" ./configure --prefix=/opt/pq --without-readline --without-icu --without-zlib --with-openssl > /dev/null \
    && make -s -C src/include install \
    && make -s -C src/common install \
    && make -s -C src/port install \
    && make -s -C src/interfaces/libpq install \
    && rm -rf /tmp/postgresql-${PG_VERSION}

ENV MMSOURCE20=/deps/metamod-source \
    HL2SDKCS2=/deps/hl2sdk-cs2 \
    PQ_ROOT=/opt/pq

WORKDIR /build

CMD ["sh", "-c", "python3 /src/configure.py -s cs2 --enable-optimize && ambuild && rm -rf /out/package && cp -r package /out/"]
