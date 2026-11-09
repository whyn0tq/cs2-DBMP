#include "mysql.h"
#include "bind.h"
#include "fill.h"

#include <cstdlib>
#include <errmsg.h>
#include <mysql.h>

namespace
{
constexpr unsigned int kIoTimeout = 60;
constexpr auto kIdle = std::chrono::seconds(30);

bool IsInteger(enum_field_types type)
{
    switch (type)
    {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_YEAR:
        return true;
    default:
        return false;
    }
}

bool IsReal(enum_field_types type)
{
    return type == MYSQL_TYPE_FLOAT || type == MYSQL_TYPE_DOUBLE;
}

sql::Value Cell(const char* data, unsigned long len, const MYSQL_FIELD& field)
{
    if (!data)
        return nullptr;
    if (IsInteger(field.type))
    {
        if (field.flags & UNSIGNED_FLAG)
            return static_cast<int64_t>(std::strtoull(data, nullptr, 10));
        return static_cast<int64_t>(std::strtoll(data, nullptr, 10));
    }
    if (IsReal(field.type))
        return std::strtod(data, nullptr);
    return std::string(data, len);
}
}

MySql::MySql(sql::Config cfg) : m_cfg(std::move(cfg)) {}

MySql::~MySql()
{
    Close();
}

void MySql::Attach()
{
    mysql_thread_init();
}

void MySql::Detach()
{
    Close();
    mysql_thread_end();
}

sql::Result MySql::Run(const std::string& text, const sql::Args& args)
{
    std::string error;
    if (!Ready(error))
        return sql::Fill::Fail(error);

    ::Quote quote = [this](std::string_view s) { return Quote(s); };
    std::string bound;
    if (!Bind(text, args.size(), Dialect::MySql, [&](size_t i) { return Literal(args[i], quote); }, bound, error))
        return sql::Fill::Fail(error);

    return Send(bound);
}

sql::Result MySql::Begin()
{
    std::string error;
    if (!Ready(error))
        return sql::Fill::Fail(error);

    sql::Result r = Send("START TRANSACTION");
    m_tx = r.Ok();
    return r;
}

sql::Result MySql::Commit()
{
    return End("COMMIT");
}

sql::Result MySql::Rollback()
{
    return End("ROLLBACK");
}

std::string MySql::Escape(std::string_view s) const
{
    std::string out;
    out.reserve(s.size() + 8);

    if (m_plain)
    {
        for (char c : s)
        {
            if (c == '\'')
                out += '\'';
            out += c;
        }
        return out;
    }

    for (char c : s)
    {
        switch (c)
        {
        case '\0':
            out += "\\0";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\'':
            out += "\\'";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\032':
            out += "\\Z";
            break;
        default:
            out += c;
        }
    }
    return out;
}

void MySql::Close()
{
    if (m_db)
        mysql_close(m_db);
    m_db = nullptr;
    m_tx = false;
}

bool MySql::Connect(std::string& error)
{
    m_db = mysql_init(nullptr);
    if (!m_db)
    {
        error = "out of memory";
        return false;
    }

    unsigned int timeout = static_cast<unsigned int>(m_cfg.timeout > 0 ? m_cfg.timeout : 10);
    unsigned int io = kIoTimeout;
    mysql_options(m_db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(m_db, MYSQL_OPT_READ_TIMEOUT, &io);
    mysql_options(m_db, MYSQL_OPT_WRITE_TIMEOUT, &io);
    mysql_options(m_db, MYSQL_SET_CHARSET_NAME, m_cfg.charset.c_str());

    const char* name = m_cfg.name.empty() ? nullptr : m_cfg.name.c_str();
    unsigned int port = static_cast<unsigned int>(m_cfg.port > 0 ? m_cfg.port : 0);
    if (!mysql_real_connect(m_db, m_cfg.host.c_str(), m_cfg.user.c_str(), m_cfg.pass.c_str(), name, port, nullptr, 0))
    {
        error = mysql_error(m_db);
        Close();
        return false;
    }

    m_plain = (m_db->server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES) != 0;
    m_used = std::chrono::steady_clock::now();
    return true;
}

bool MySql::Ready(std::string& error)
{
    if (m_db && !m_tx && std::chrono::steady_clock::now() - m_used > kIdle && mysql_ping(m_db) != 0)
        Close();
    return m_db || Connect(error);
}

sql::Result MySql::End(const char* text)
{
    if (!m_db)
    {
        m_tx = false;
        return sql::Fill::Fail("connection lost");
    }

    sql::Result r = Send(text);
    m_tx = false;
    return r;
}

sql::Result MySql::Send(const std::string& text)
{
    if (mysql_real_query(m_db, text.data(), text.size()) != 0)
    {
        unsigned int code = mysql_errno(m_db);
        std::string error = mysql_error(m_db);
        if (code != CR_SERVER_GONE_ERROR && code != CR_SERVER_LOST)
            return sql::Fill::Fail(error);

        bool retry = code == CR_SERVER_GONE_ERROR && !m_tx;
        Close();
        if (!retry || !Connect(error))
            return sql::Fill::Fail(error);
        if (mysql_real_query(m_db, text.data(), text.size()) != 0)
            return sql::Fill::Fail(mysql_error(m_db));
    }

    m_used = std::chrono::steady_clock::now();
    m_plain = (m_db->server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES) != 0;
    return Read();
}

sql::Result MySql::Read()
{
    sql::Result r;
    MYSQL_RES* res = mysql_store_result(m_db);
    if (res)
    {
        unsigned int count = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);

        std::vector<std::string> names;
        names.reserve(count);
        for (unsigned int i = 0; i < count; i++)
            names.emplace_back(fields[i].name);
        sql::Fill::Names(r, std::move(names));

        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            unsigned long* lens = mysql_fetch_lengths(res);
            std::vector<sql::Value> cells;
            cells.reserve(count);
            for (unsigned int i = 0; i < count; i++)
                cells.push_back(Cell(row[i], lens[i], fields[i]));
            sql::Fill::Row(r, std::move(cells));
        }
        mysql_free_result(res);
    }
    else if (mysql_field_count(m_db) != 0)
    {
        return sql::Fill::Fail(mysql_error(m_db));
    }

    sql::Fill::Affected(r, static_cast<int64_t>(mysql_affected_rows(m_db)));
    sql::Fill::Id(r, static_cast<int64_t>(mysql_insert_id(m_db)));

    while (mysql_next_result(m_db) == 0)
    {
        if (MYSQL_RES* extra = mysql_store_result(m_db))
            mysql_free_result(extra);
    }
    return r;
}

std::string MySql::Quote(std::string_view s) const
{
    return "'" + Escape(s) + "'";
}
