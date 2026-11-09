#include "bind.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace
{
size_t SkipQuoted(std::string_view text, size_t i, bool backslash)
{
    char q = text[i];
    for (i++; i < text.size(); i++)
    {
        if (backslash && text[i] == '\\' && q != '`')
            i++;
        else if (text[i] == q)
            return i;
    }
    return text.size() - 1;
}

size_t SkipLine(std::string_view text, size_t i)
{
    size_t end = text.find('\n', i);
    return end == std::string_view::npos ? text.size() - 1 : end;
}

size_t SkipBlock(std::string_view text, size_t i)
{
    size_t end = text.find("*/", i + 2);
    return end == std::string_view::npos ? text.size() - 1 : end + 1;
}

bool IsWord(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

size_t SkipDollar(std::string_view text, size_t i)
{
    if (i > 0 && IsWord(text[i - 1]))
        return i;

    size_t j = i + 1;
    while (j < text.size() && IsWord(text[j]))
        j++;
    if (j >= text.size() || text[j] != '$' || (j > i + 1 && std::isdigit(static_cast<unsigned char>(text[i + 1]))))
        return i;

    std::string_view tag = text.substr(i, j - i + 1);
    size_t end = text.find(tag, j + 1);
    return end == std::string_view::npos ? text.size() - 1 : end + tag.size() - 1;
}

bool IsLineComment(std::string_view text, size_t i, bool backslash)
{
    if (text[i] == '#')
        return backslash;
    if (text[i] != '-' || i + 1 >= text.size() || text[i + 1] != '-')
        return false;
    return !backslash || i + 2 >= text.size() || std::isspace(static_cast<unsigned char>(text[i + 2]));
}
}

std::string Literal(const sql::Value& v, const Quote& quote)
{
    const auto& raw = v.Raw();
    if (auto i = std::get_if<int64_t>(&raw))
        return std::to_string(*i);
    if (auto d = std::get_if<double>(&raw))
    {
        if (!std::isfinite(*d))
            return "NULL";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", *d);
        return buf;
    }
    if (auto s = std::get_if<std::string>(&raw))
        return quote(*s);
    return "NULL";
}

bool Bind(std::string_view text, size_t count, Dialect dialect, const Place& place, std::string& out, std::string& error)
{
    bool backslash = dialect == Dialect::MySql;
    out.clear();
    out.reserve(text.size() + count * 8);

    size_t used = 0;
    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];
        size_t end = i;

        if (c == '\'' || c == '"' || (backslash && c == '`'))
            end = SkipQuoted(text, i, backslash);
        else if (IsLineComment(text, i, backslash))
            end = SkipLine(text, i);
        else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
            end = SkipBlock(text, i);
        else if (!backslash && c == '$')
            end = SkipDollar(text, i);
        else if (c == '?')
        {
            if (used >= count)
            {
                error = "not enough arguments for placeholders";
                return false;
            }
            out += place(used++);
            continue;
        }

        out.append(text.substr(i, end - i + 1));
        i = end;
    }

    if (used != count)
    {
        error = "too many arguments for placeholders";
        return false;
    }
    return true;
}
