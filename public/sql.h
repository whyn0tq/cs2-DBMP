#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#define SQL_INTERFACE "SqlApi001"
#define SQL_INTERFACE_V2 "SqlApi002"

namespace sql
{
class Value
{
public:
    using Data = std::variant<std::nullptr_t, int64_t, double, std::string>;

    Value() : m_data(nullptr) {}
    Value(std::nullptr_t) : m_data(nullptr) {}
    Value(bool v) : m_data(static_cast<int64_t>(v)) {}
    Value(const char* v) : m_data(v ? Data(std::string(v)) : Data(nullptr)) {}
    Value(std::string_view v) : m_data(std::string(v)) {}
    Value(std::string v) : m_data(std::move(v)) {}

    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T v) : m_data(static_cast<int64_t>(v))
    {
    }

    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Value(T v) : m_data(static_cast<double>(v))
    {
    }

    bool Null() const { return std::holds_alternative<std::nullptr_t>(m_data); }

    int64_t Int() const
    {
        if (auto v = std::get_if<int64_t>(&m_data))
            return *v;
        if (auto v = std::get_if<double>(&m_data))
            return static_cast<int64_t>(*v);
        if (auto v = std::get_if<std::string>(&m_data))
            return std::strtoll(v->c_str(), nullptr, 10);
        return 0;
    }

    double Real() const
    {
        if (auto v = std::get_if<double>(&m_data))
            return *v;
        if (auto v = std::get_if<int64_t>(&m_data))
            return static_cast<double>(*v);
        if (auto v = std::get_if<std::string>(&m_data))
            return std::strtod(v->c_str(), nullptr);
        return 0.0;
    }

    std::string Str() const
    {
        if (auto v = std::get_if<std::string>(&m_data))
            return *v;
        if (auto v = std::get_if<int64_t>(&m_data))
            return std::to_string(*v);
        if (auto v = std::get_if<double>(&m_data))
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.15g", *v);
            return buf;
        }
        return {};
    }

    const Data& Raw() const { return m_data; }

private:
    Data m_data;
};

using Args = std::vector<Value>;

class Result
{
public:
    bool Ok() const { return m_error.empty(); }
    const std::string& Error() const { return m_error; }

    int Rows() const { return static_cast<int>(m_rows.size()); }
    int Cols() const { return static_cast<int>(m_names.size()); }
    int64_t Id() const { return m_id; }
    int64_t Affected() const { return m_affected; }

    const std::string& Name(int col) const
    {
        static const std::string empty;
        return col >= 0 && col < Cols() ? m_names[col] : empty;
    }

    int Col(std::string_view name) const
    {
        for (int i = 0; i < Cols(); i++)
        {
            if (m_names[i] == name)
                return i;
        }
        return -1;
    }

    bool Next()
    {
        if (m_cursor + 1 >= Rows())
        {
            m_cursor = Rows();
            return false;
        }
        m_cursor++;
        return true;
    }

    void Rewind() { m_cursor = -1; }

    const Value& Get(int col) const
    {
        static const Value null;
        if (m_cursor < 0 || m_cursor >= Rows() || col < 0 || col >= Cols())
            return null;
        return m_rows[m_cursor][col];
    }

    const Value& Get(std::string_view name) const { return Get(Col(name)); }

    bool Null(int col) const { return Get(col).Null(); }
    bool Null(std::string_view name) const { return Get(name).Null(); }
    int64_t Int(int col) const { return Get(col).Int(); }
    int64_t Int(std::string_view name) const { return Get(name).Int(); }
    double Real(int col) const { return Get(col).Real(); }
    double Real(std::string_view name) const { return Get(name).Real(); }
    std::string Str(int col) const { return Get(col).Str(); }
    std::string Str(std::string_view name) const { return Get(name).Str(); }

private:
    friend class Fill;

    std::string m_error;
    std::vector<std::string> m_names;
    std::vector<std::vector<Value>> m_rows;
    int64_t m_id = 0;
    int64_t m_affected = 0;
    int m_cursor = -1;
};

struct Stmt
{
    std::string sql;
    Args args;
};

class Tx
{
public:
    Tx& Add(std::string sql, Args args = {})
    {
        m_items.push_back({std::move(sql), std::move(args)});
        return *this;
    }

    bool Empty() const { return m_items.empty(); }
    size_t Size() const { return m_items.size(); }
    const std::vector<Stmt>& Items() const { return m_items; }

private:
    std::vector<Stmt> m_items;
};

enum class Kind
{
    MySQL,
    SQLite,
    PostgreSQL
};

struct Config
{
    Kind kind = Kind::SQLite;
    std::string host = "127.0.0.1";
    std::string user;
    std::string pass;
    std::string name;
    std::string charset = "utf8mb4";
    int port = 0;
    int timeout = 10;
};

using OnConnect = std::function<void(bool ok, const std::string& error)>;
using OnResult = std::function<void(Result&)>;
using OnCommit = std::function<void(bool ok, std::vector<Result>& results)>;

class IConn
{
public:
    virtual void Connect(OnConnect cb) = 0;
    virtual void Query(std::string sql, Args args, OnResult cb) = 0;
    virtual void Commit(Tx tx, OnCommit cb) = 0;
    virtual std::string Escape(std::string_view s) const = 0;
    virtual Kind Type() const = 0;
    virtual void Close() = 0;

    void Connect(std::function<void(bool ok)> cb)
    {
        Connect(OnConnect([cb = std::move(cb)](bool ok, const std::string&) {
            if (cb)
                cb(ok);
        }));
    }

    void Query(std::string sql, OnResult cb) { Query(std::move(sql), Args{}, std::move(cb)); }
    void Exec(std::string sql, Args args = {}) { Query(std::move(sql), std::move(args), nullptr); }

protected:
    ~IConn() = default;
};

class IApi
{
public:
    virtual IConn* Open(const Config& cfg) = 0;
    virtual const char* Version() const = 0;

protected:
    ~IApi() = default;
};

class IApiV2 : public IApi
{
public:
    using IApi::Open;
    virtual IConn* Open(std::string_view database) = 0;

protected:
    ~IApiV2() = default;
};
}
