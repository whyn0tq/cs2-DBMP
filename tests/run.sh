#!/bin/sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/dbmm-test}
PQ=${PQ_ROOT:-/opt/pq}
CXX=${CXX:-clang++}
CC=${CC:-clang}

mkdir -p "$OUT"
LIB=$(for f in mariadbclient mysqlclient; do p=/usr/lib/x86_64-linux-gnu/lib$f.a; [ -f "$p" ] && echo "$p" && break; done)

$CC -O1 -w -fPIC $FLAGS -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 -c "$ROOT/vendor/sqlite/sqlite3.c" -o "$OUT/sqlite3.o"

$CXX -std=c++17 -O1 -g -Wall -Werror -pthread $FLAGS \
    -I"$ROOT/tests" -I"$ROOT/src" -I"$ROOT/public" -I"$ROOT/vendor/sqlite" -I"$PQ/include" $(mariadb_config --include) \
    "$ROOT/tests/test.cpp" \
    "$ROOT/src/api.cpp" "$ROOT/src/conn.cpp" "$ROOT/src/registry.cpp" "$ROOT/src/dispatcher.cpp" \
    "$ROOT/src/worker.cpp" \
    "$ROOT/src/backend/factory.cpp" "$ROOT/src/backend/bind.cpp" "$ROOT/src/backend/mysql.cpp" \
    "$ROOT/src/backend/pg.cpp" "$ROOT/src/backend/sqlite.cpp" \
    "$OUT/sqlite3.o" "$LIB" "$PQ/lib/libpq.a" "$PQ/lib/libpgcommon_shlib.a" "$PQ/lib/libpgport_shlib.a" \
    -lssl -lcrypto -lz -ldl -lm \
    -o "$OUT/test"

cd "$OUT" && ./test
