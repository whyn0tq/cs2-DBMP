#pragma once

#include <functional>
#include <sql.h>
#include <string>
#include <string_view>

enum class Dialect
{
    MySql,
    Pg
};

using Quote = std::function<std::string(std::string_view)>;
using Place = std::function<std::string(size_t index)>;

std::string Literal(const sql::Value& v, const Quote& quote);
bool Bind(std::string_view text, size_t count, Dialect dialect, const Place& place, std::string& out, std::string& error);
