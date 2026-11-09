#pragma once

#include <sql.h>
#include <string>
#include <utility>
#include <vector>

namespace sql
{
class Fill
{
public:
    static Result Fail(std::string error)
    {
        Result r;
        r.m_error = error.empty() ? "unknown error" : std::move(error);
        return r;
    }

    static void Names(Result& r, std::vector<std::string> names) { r.m_names = std::move(names); }
    static void Row(Result& r, std::vector<Value> row) { r.m_rows.push_back(std::move(row)); }
    static void Id(Result& r, int64_t id) { r.m_id = id; }
    static void Affected(Result& r, int64_t n) { r.m_affected = n; }
};
}
