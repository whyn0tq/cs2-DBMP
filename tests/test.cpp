#include "api.h"
#include "backend/bind.h"
#include "dispatcher.h"
#include "registry.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mysql.h>
#include <string>
#include <thread>

void Log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::printf("  log: ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
}

namespace
{
int g_passed = 0;
int g_failed = 0;

void Check(bool ok, const std::string& what)
{
    if (ok)
        g_passed++;
    else
    {
        g_failed++;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

struct Env
{
    int owner = 1;
    Dispatcher out;
    Registry conns;
    Api api{conns, out, "/tmp/dbmm-test", [this](const void*) { return owner; }};

    bool Pump(const std::function<bool()>& until, int seconds = 15)
    {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!until() && std::chrono::steady_clock::now() < end)
        {
            out.Run();
            conns.Reap();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return until();
    }

    sql::Result Wait(sql::IConn* db, std::string text, sql::Args args = {})
    {
        sql::Result result;
        bool done = false;
        db->Query(std::move(text), std::move(args), [&](sql::Result& r) {
            result = r;
            done = true;
        });
        Check(Pump([&] { return done; }), "query finished in time");
        return result;
    }
};

void TestBind()
{
    std::printf("== bind\n");
    Quote quote = [](std::string_view s) { return "'" + std::string(s) + "'"; };
    std::string out, error;
    auto my = [&](std::string_view text, const sql::Args& args) {
        return Bind(text, args.size(), Dialect::MySql, [&](size_t i) { return Literal(args[i], quote); }, out, error);
    };
    auto pg = [&](std::string_view text, size_t count) {
        return Bind(text, count, Dialect::Pg, [](size_t i) { return "$" + std::to_string(i + 1); }, out, error);
    };

    Check(my("SELECT ?, '?', \"?\", `a?` -- ?\n, ? /* ? */ # ?", {1, "x"}) &&
              out == "SELECT 1, '?', \"?\", `a?` -- ?\n, 'x' /* ? */ # ?",
          "placeholders skip literals and comments");
    Check(my("SELECT 'it\\'s ?', ?", {2}) && out == "SELECT 'it\\'s ?', 2", "escaped quote inside literal");
    Check(my("SELECT ?, ?, ?", {nullptr, 1.5, true}) && out == "SELECT NULL, 1.5, 1", "null, real and bool");
    Check(!my("SELECT ?", {}), "missing argument rejected");
    Check(!my("SELECT 1", {1}), "extra argument rejected");

    Check(pg("SELECT ?, 'C:\\', ? -- ?\n, $$ ? $$, $fn$ ? $fn$, a$b", 2) &&
              out == "SELECT $1, 'C:\\', $2 -- ?\n, $$ ? $$, $fn$ ? $fn$, a$b",
          "postgres placeholders, backslash and dollar quotes");
    Check(pg("SELECT 5 # ?", 1) && out == "SELECT 5 # $1", "postgres # is an operator");
}

void TestValues()
{
    std::printf("== values\n");
    sql::Value big = uint64_t(76561198000000000ULL);
    Check(big.Int() == 76561198000000000LL, "uint64 steamid");
    Check(sql::Value("42").Int() == 42 && sql::Value(2.5).Str() == "2.5" && sql::Value(7).Real() == 7.0, "conversions");
    Check(sql::Value(static_cast<const char*>(nullptr)).Null() && sql::Value().Null(), "null values");
}

void Suite(Env& env, const sql::Config& cfg, const char* tag, const char* create, const std::string& escaped, bool ids)
{
    std::printf("== %s\n", tag);
    sql::IConn* db = env.api.Open(cfg);
    Check(db != nullptr, "open");
    if (!db)
        return;

    int calls = 0;
    bool full = false;
    bool plain = false;
    std::string why = "unset";
    db->Connect([&](bool ok, const std::string& error) {
        full = ok;
        why = error;
        calls++;
    });
    db->Connect([&](bool ok) {
        plain = ok;
        calls++;
    });
    Check(env.Pump([&] { return calls == 2; }), "connect callbacks fired");
    Check(full && why.empty() && plain, "connect succeeds: " + why);

    sql::Config wrong = cfg;
    if (cfg.kind == sql::Kind::SQLite)
        wrong.name = "/proc/dbmm/test.db";
    else
        wrong.pass = "wrong";
    sql::IConn* broken = env.api.Open(wrong);
    bool failed = false;
    bool done = false;
    broken->Connect([&](bool ok, const std::string& error) {
        failed = !ok;
        why = error;
        done = true;
    });
    Check(env.Pump([&] { return done; }), "failed connect reported in time");
    Check(failed && !why.empty(), "connect failure has a reason: " + why);
    broken->Close();

    sql::Result r = env.Wait(db, "DROP TABLE IF EXISTS t");
    Check(r.Ok(), "drop table: " + r.Error());
    r = env.Wait(db, create);
    Check(r.Ok(), "create table: " + r.Error());

    std::string tricky = "O'Brien \"x\" \\ ; \xF0\x9F\x98\x80";
    r = env.Wait(db, "INSERT INTO t (name, score, ratio) VALUES (?, ?, ?)", {tricky, uint64_t(76561198000000000ULL), 0.25});
    Check(r.Ok() && (!ids || r.Id() == 1) && r.Affected() == 1, "insert returns id and affected: " + r.Error());

    db->Exec("INSERT INTO t (name, score, ratio) VALUES (?, ?, ?)", {nullptr, -5, nullptr});

    r = env.Wait(db, "SELECT id, name, score, ratio FROM t ORDER BY id");
    Check(r.Ok() && r.Rows() == 2 && r.Cols() == 4, "select shape: " + r.Error());
    Check(r.Next(), "first row");
    Check(r.Str("name") == tricky, "string roundtrip");
    Check(r.Int("score") == 76561198000000000LL, "int64 roundtrip");
    Check(r.Real(3) == 0.25, "real roundtrip");
    Check(r.Next() && r.Null("name") && r.Null(3) && r.Int(2) == -5, "null roundtrip");
    Check(!r.Next(), "end of rows");
    Check(r.Col("score") == 2 && r.Col("nope") == -1 && r.Name(1) == "name", "column lookup");
    r.Rewind();
    Check(r.Next() && r.Int(0) == 1, "rewind");

    r = env.Wait(db, "SELECT name FROM t WHERE name = '" + db->Escape(tricky) + "'");
    Check(r.Ok() && r.Rows() == 1, "manual escape works: " + r.Error());
    Check(db->Escape("a'b") == escaped, "escape format");

    r = env.Wait(db, "SELECT ?", {});
    Check(!r.Ok(), "missing argument is an error");
    r = env.Wait(db, "SELEC broken");
    Check(!r.Ok() && !r.Error().empty(), "syntax error reported");

    done = false;
    sql::Tx good;
    good.Add("INSERT INTO t (name) VALUES (?)", {"tx1"}).Add("UPDATE t SET score = ? WHERE name = ?", {7, "tx1"});
    db->Commit(std::move(good), [&](bool ok, std::vector<sql::Result>& rs) {
        Check(ok && rs.size() == 2 && rs[1].Affected() == 1, "transaction commits");
        done = true;
    });
    env.Pump([&] { return done; });

    done = false;
    sql::Tx bad;
    bad.Add("INSERT INTO t (name) VALUES (?)", {"tx2"}).Add("INSERT INTO missing_table VALUES (1)");
    db->Commit(std::move(bad), [&](bool ok, std::vector<sql::Result>& rs) {
        Check(!ok && rs.size() == 2 && !rs.back().Ok(), "transaction failure reported");
        done = true;
    });
    env.Pump([&] { return done; });

    r = env.Wait(db, "SELECT COUNT(*) AS n FROM t WHERE name = ?", {"tx2"});
    Check(r.Next() && r.Int("n") == 0, "failed transaction rolled back");

    bool called = false;
    db->Exec("INSERT INTO t (name) VALUES ('flushed')");
    db->Query("SELECT 1", [&](sql::Result&) { called = true; });
    db->Close();
    Check(env.Pump([&] { return env.conns.Size() == 0; }), "closed connection reaped");
    env.out.Run();
    Check(!called, "callbacks dropped after close");

    sql::IConn* again = env.api.Open(cfg);
    r = env.Wait(again, "SELECT COUNT(*) FROM t WHERE name = 'flushed'");
    Check(r.Next() && r.Int(0) == 1, "queued writes flushed on close");

    called = false;
    env.owner = 2;
    sql::IConn* other = env.api.Open(cfg);
    env.owner = 1;
    other->Query("SELECT 1", [&](sql::Result&) { called = true; });
    env.conns.Drop(2);
    Check(env.Pump([&] { return env.conns.Size() == 1; }), "owner connections dropped");
    Check(!called, "owner callbacks never fire");

    again->Close();
    env.Pump([&] { return env.conns.Size() == 0; });
}

void TestPg(Env& env, const sql::Config& cfg)
{
    std::printf("== postgresql extras\n");
    sql::IConn* db = env.api.Open(cfg);

    sql::Result r = env.Wait(db, "INSERT INTO t (name) VALUES (?) RETURNING id", {"returning"});
    Check(r.Ok() && r.Next() && r.Int("id") > 0, "insert returning id: " + r.Error());

    r = env.Wait(db, "SELECT true AS yes, false AS no, '\\x00ff'::bytea AS raw, $$ ? $$ AS dollar, '{\"a\":1}'::jsonb ->> 'a' AS json");
    Check(r.Ok() && r.Next(), "postgres types: " + r.Error());
    Check(r.Int("yes") == 1 && r.Int("no") == 0, "bool columns");
    Check(r.Str("raw") == std::string("\x00\xff", 2), "bytea decoded");
    Check(r.Str("dollar") == " ? ", "dollar quoted text untouched");
    Check(r.Str("json") == "1", "jsonb operators");

    r = env.Wait(db, "SELECT ?::int + ?::int AS sum", {2, 3});
    Check(r.Next() && r.Int("sum") == 5, "typed parameters: " + r.Error());

    db->Close();
    env.Pump([&] { return env.conns.Size() == 0; });
}

void TestReconnect(Env& env, const sql::Config& cfg, const char* tag, const char* who, const char* kill)
{
    std::printf("== %s reconnect\n", tag);
    sql::IConn* a = env.api.Open(cfg);
    sql::IConn* b = env.api.Open(cfg);

    sql::Result r = env.Wait(a, who);
    Check(r.Next(), "connection id: " + r.Error());
    r = env.Wait(b, kill, {r.Int(0)});
    Check(r.Ok(), "kill connection: " + r.Error());

    env.Wait(a, "SELECT 1");
    r = env.Wait(a, "SELECT 2");
    Check(r.Ok() && r.Next() && r.Int(0) == 2, "recovers after connection killed: " + r.Error());

    a->Close();
    b->Close();
    env.Pump([&] { return env.conns.Size() == 0; });
}
}

int main()
{
    mysql_library_init(0, nullptr, nullptr);

    TestBind();
    TestValues();

    {
        Env env;
        sql::Config lite;
        lite.kind = sql::Kind::SQLite;
        lite.name = "data/test.db";
        Suite(env, lite, "sqlite", "CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, score INTEGER, ratio REAL)", "a''b", true);
    }

    if (const char* host = std::getenv("MYSQL_HOST"))
    {
        Env env;
        sql::Config my;
        my.kind = sql::Kind::MySQL;
        my.host = host;
        my.user = "root";
        my.pass = std::getenv("MYSQL_PASS") ? std::getenv("MYSQL_PASS") : "";
        my.name = "dbmm_test";
        Suite(env, my, "mysql", "CREATE TABLE t (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(64), score BIGINT, ratio DOUBLE) CHARACTER SET utf8mb4", "a\\'b", true);
        TestReconnect(env, my, "mysql", "SELECT CONNECTION_ID()", "KILL ?");
    }

    if (const char* host = std::getenv("PG_HOST"))
    {
        Env env;
        sql::Config pg;
        pg.kind = sql::Kind::PostgreSQL;
        pg.host = host;
        pg.user = "postgres";
        pg.pass = std::getenv("PG_PASS") ? std::getenv("PG_PASS") : "";
        pg.name = "dbmm_test";
        Suite(env, pg, "postgresql", "CREATE TABLE t (id SERIAL PRIMARY KEY, name TEXT, score BIGINT, ratio DOUBLE PRECISION)", "a''b", false);
        TestPg(env, pg);
        TestReconnect(env, pg, "postgresql", "SELECT pg_backend_pid()", "SELECT pg_terminate_backend(?)");
    }

    mysql_library_end();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
