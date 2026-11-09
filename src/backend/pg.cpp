#include "pg.h"
#include "bind.h"
#include "fill.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <libpq-fe.h>
#include <vector>

namespace
{
constexpr auto kIdle = std::chrono::seconds(30);

enum : Oid
{
    kBool = 16,
    kBytea = 17,
    kInt8 = 20,
    kInt2 = 21,
    kInt4 = 23,
    kOid = 26,
    kFloat4 = 700,
    kFloat8 = 701
};

std::string Trim(const char* msg)
{
    std::string s = msg ? msg : "";
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

sql::Value Cell(PGresult* res, int row, int col)
{
    if (PQgetisnull(res, row, col))
        return nullptr;

    const char* data = PQgetvalue(res, row, col);
    int len = PQgetlength(res, row, col);
    switch (PQftype(res, col))
    {
    case kInt2:
    case kInt4:
    case kInt8:
    case kOid:
        return static_cast<int64_t>(std::strtoll(data, nullptr, 10));
    case kFloat4:
    case kFloat8:
        return std::strtod(data, nullptr);
    case kBool:
        return static_cast<int64_t>(data[0] == 't');
    case kBytea:
    {
        size_t size = 0;
        unsigned char* raw = PQunescapeBytea(reinterpret_cast<const unsigned char*>(data), &size);
        if (!raw)
            return std::string(data, len);
        std::string out(reinterpret_cast<const char*>(raw), size);
        PQfreemem(raw);
        return out;
    }
    default:
        return std::string(data, len);
    }
}

std::string Text(const sql::Value& v)
{
    const auto& raw = v.Raw();
    if (auto i = std::get_if<int64_t>(&raw))
        return std::to_string(*i);
    if (auto d = std::get_if<double>(&raw))
    {
        if (std::isnan(*d))
            return "NaN";
        if (std::isinf(*d))
            return *d > 0 ? "Infinity" : "-Infinity";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", *d);
        return buf;
    }
    if (auto s = std::get_if<std::string>(&raw))
        return *s;
    return {};
}
}

Pg::Pg(sql::Config cfg) : m_cfg(std::move(cfg)) {}

Pg::~Pg()
{
    Close();
}

void Pg::Detach()
{
    Close();
}

sql::Result Pg::Run(const std::string& text, const sql::Args& args)
{
    std::string error;
    if (!Ready(error))
        return sql::Fill::Fail(error);

    std::string bound;
    if (!Bind(text, args.size(), Dialect::Pg, [](size_t i) { return "$" + std::to_string(i + 1); }, bound, error))
        return sql::Fill::Fail(error);

    return Send(bound, args);
}

sql::Result Pg::Begin()
{
    std::string error;
    if (!Ready(error))
        return sql::Fill::Fail(error);

    sql::Result r = Send("BEGIN", {});
    m_tx = r.Ok();
    return r;
}

sql::Result Pg::Commit()
{
    return End("COMMIT");
}

sql::Result Pg::Rollback()
{
    return End("ROLLBACK");
}

std::string Pg::Escape(std::string_view s) const
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '\'')
            out += '\'';
        out += c;
    }
    return out;
}

void Pg::Close()
{
    if (m_db)
        PQfinish(m_db);
    m_db = nullptr;
    m_tx = false;
}

bool Pg::Connect(std::string& error)
{
    std::string port = m_cfg.port > 0 ? std::to_string(m_cfg.port) : std::string();
    std::string timeout = std::to_string(m_cfg.timeout > 0 ? m_cfg.timeout : 10);
    std::string charset = m_cfg.charset.empty() || m_cfg.charset == "utf8mb4" ? "UTF8" : m_cfg.charset;

    const char* keys[] = {"host", "port", "user", "password", "dbname", "connect_timeout", "client_encoding", nullptr};
    const char* values[] = {
        m_cfg.host.c_str(),
        port.c_str(),
        m_cfg.user.c_str(),
        m_cfg.pass.c_str(),
        m_cfg.name.c_str(),
        timeout.c_str(),
        charset.c_str(),
        nullptr,
    };

    m_db = PQconnectdbParams(keys, values, 0);
    if (!m_db || PQstatus(m_db) != CONNECTION_OK)
    {
        error = m_db ? Trim(PQerrorMessage(m_db)) : "out of memory";
        Close();
        return false;
    }

    PQsetNoticeProcessor(m_db, [](void*, const char*) {}, nullptr);
    m_used = std::chrono::steady_clock::now();
    return true;
}

bool Pg::Ready(std::string& error)
{
    if (m_db && !m_tx && std::chrono::steady_clock::now() - m_used > kIdle)
    {
        PGresult* res = PQexec(m_db, "");
        bool alive = PQresultStatus(res) == PGRES_EMPTY_QUERY;
        PQclear(res);
        if (!alive)
            Close();
    }
    return m_db || Connect(error);
}

sql::Result Pg::End(const char* text)
{
    if (!m_db)
    {
        m_tx = false;
        return sql::Fill::Fail("connection lost");
    }

    sql::Result r = Send(text, {});
    m_tx = false;
    return r;
}

sql::Result Pg::Send(const std::string& text, const sql::Args& args)
{
    PGresult* res = nullptr;
    if (args.empty())
        res = PQexec(m_db, text.c_str());
    else
    {
        std::vector<std::string> texts(args.size());
        std::vector<const char*> values(args.size(), nullptr);
        for (size_t i = 0; i < args.size(); i++)
        {
            if (args[i].Null())
                continue;
            texts[i] = Text(args[i]);
            values[i] = texts[i].c_str();
        }
        res = PQexecParams(m_db, text.c_str(), static_cast<int>(args.size()), nullptr, values.data(), nullptr, nullptr, 0);
    }

    sql::Result r = Read(res);
    PQclear(res);

    if (PQstatus(m_db) == CONNECTION_BAD)
        Close();
    else
        m_used = std::chrono::steady_clock::now();
    return r;
}

sql::Result Pg::Read(PGresult* res)
{
    switch (PQresultStatus(res))
    {
    case PGRES_TUPLES_OK:
        break;
    case PGRES_COMMAND_OK:
    case PGRES_EMPTY_QUERY:
    {
        sql::Result r;
        sql::Fill::Affected(r, std::strtoll(PQcmdTuples(res), nullptr, 10));
        return r;
    }
    default:
        return sql::Fill::Fail(Trim(res ? PQresultErrorMessage(res) : PQerrorMessage(m_db)));
    }

    sql::Result r;
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    std::vector<std::string> names;
    names.reserve(cols);
    for (int i = 0; i < cols; i++)
        names.emplace_back(PQfname(res, i));
    sql::Fill::Names(r, std::move(names));

    for (int row = 0; row < rows; row++)
    {
        std::vector<sql::Value> cells;
        cells.reserve(cols);
        for (int col = 0; col < cols; col++)
            cells.push_back(Cell(res, row, col));
        sql::Fill::Row(r, std::move(cells));
    }

    sql::Fill::Affected(r, rows);
    return r;
}
