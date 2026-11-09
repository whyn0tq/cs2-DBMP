#include "sqlite.h"
#include "fill.h"

#include <cctype>
#include <filesystem>
#include <sqlite3.h>

namespace
{
bool Blank(const char* s)
{
    for (; s && *s; s++)
    {
        if (!std::isspace(static_cast<unsigned char>(*s)))
            return false;
    }
    return true;
}

sql::Value Cell(sqlite3_stmt* stmt, int col)
{
    switch (sqlite3_column_type(stmt, col))
    {
    case SQLITE_INTEGER:
        return static_cast<int64_t>(sqlite3_column_int64(stmt, col));
    case SQLITE_FLOAT:
        return sqlite3_column_double(stmt, col);
    case SQLITE_TEXT:
    {
        auto data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return std::string(data, sqlite3_column_bytes(stmt, col));
    }
    case SQLITE_BLOB:
    {
        auto data = static_cast<const char*>(sqlite3_column_blob(stmt, col));
        int size = sqlite3_column_bytes(stmt, col);
        return data ? std::string(data, size) : std::string();
    }
    default:
        return nullptr;
    }
}
}

Sqlite::Sqlite(sql::Config cfg) : m_cfg(std::move(cfg)) {}

Sqlite::~Sqlite()
{
    Close();
}

bool Sqlite::Ready(std::string& error)
{
    return m_db || Open(error);
}

sql::Result Sqlite::Run(const std::string& text, const sql::Args& args)
{
    std::string error;
    if (!Ready(error))
        return sql::Fill::Fail(error);

    sql::Result last;
    size_t used = 0;
    const char* tail = text.c_str();
    const char* end = tail + text.size();

    while (!Blank(tail))
    {
        sqlite3_stmt* stmt = nullptr;
        const char* next = nullptr;
        if (sqlite3_prepare_v2(m_db, tail, static_cast<int>(end - tail), &stmt, &next) != SQLITE_OK)
            return sql::Fill::Fail(sqlite3_errmsg(m_db));

        tail = next;
        if (!stmt)
            continue;

        if (!Apply(stmt, args, used, Blank(tail), error))
        {
            sqlite3_finalize(stmt);
            return sql::Fill::Fail(error);
        }

        sql::Result r = Step(stmt);
        sqlite3_finalize(stmt);
        if (!r.Ok())
            return r;
        if (r.Cols() > 0 || last.Cols() == 0)
            last = std::move(r);
    }

    if (used != args.size())
        return sql::Fill::Fail("too many arguments for placeholders");
    return last;
}

sql::Result Sqlite::Begin()
{
    return Run("BEGIN", {});
}

sql::Result Sqlite::Commit()
{
    return Run("COMMIT", {});
}

sql::Result Sqlite::Rollback()
{
    return Run("ROLLBACK", {});
}

std::string Sqlite::Escape(std::string_view s) const
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

void Sqlite::Close()
{
    if (m_db)
        sqlite3_close_v2(m_db);
    m_db = nullptr;
}

bool Sqlite::Open(std::string& error)
{
    std::filesystem::path path(m_cfg.name);
    if (path.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(m_cfg.name.c_str(), &m_db, flags, nullptr) != SQLITE_OK)
    {
        error = m_db ? sqlite3_errmsg(m_db) : "out of memory";
        Close();
        return false;
    }

    sqlite3_busy_timeout(m_db, (m_cfg.timeout > 0 ? m_cfg.timeout : 10) * 1000);
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    return true;
}

bool Sqlite::Apply(sqlite3_stmt* stmt, const sql::Args& args, size_t& used, bool last, std::string& error)
{
    size_t count = static_cast<size_t>(sqlite3_bind_parameter_count(stmt));
    if (used + count > args.size())
    {
        error = "not enough arguments for placeholders";
        return false;
    }
    if (last && used + count != args.size())
    {
        error = "too many arguments for placeholders";
        return false;
    }

    for (size_t i = 1; i <= count; i++)
    {
        const auto& raw = args[used++].Raw();
        int idx = static_cast<int>(i);
        int rc;
        if (auto v = std::get_if<int64_t>(&raw))
            rc = sqlite3_bind_int64(stmt, idx, *v);
        else if (auto v = std::get_if<double>(&raw))
            rc = sqlite3_bind_double(stmt, idx, *v);
        else if (auto v = std::get_if<std::string>(&raw))
            rc = sqlite3_bind_text(stmt, idx, v->data(), static_cast<int>(v->size()), SQLITE_TRANSIENT);
        else
            rc = sqlite3_bind_null(stmt, idx);

        if (rc != SQLITE_OK)
        {
            error = sqlite3_errmsg(m_db);
            return false;
        }
    }
    return true;
}

sql::Result Sqlite::Step(sqlite3_stmt* stmt)
{
    sql::Result r;
    int cols = sqlite3_column_count(stmt);
    if (cols > 0)
    {
        std::vector<std::string> names;
        names.reserve(cols);
        for (int i = 0; i < cols; i++)
            names.emplace_back(sqlite3_column_name(stmt, i));
        sql::Fill::Names(r, std::move(names));
    }

    for (;;)
    {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return sql::Fill::Fail(sqlite3_errmsg(m_db));

        std::vector<sql::Value> cells;
        cells.reserve(cols);
        for (int i = 0; i < cols; i++)
            cells.push_back(Cell(stmt, i));
        sql::Fill::Row(r, std::move(cells));
    }

    sql::Fill::Affected(r, cols > 0 ? r.Rows() : static_cast<int64_t>(sqlite3_changes64(m_db)));
    sql::Fill::Id(r, static_cast<int64_t>(sqlite3_last_insert_rowid(m_db)));
    return r;
}
