#include "database_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
struct Node
{
    std::unordered_map<std::string, std::string> values;
    std::unordered_map<std::string, std::unique_ptr<Node>> sections;
};

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

class Lexer
{
public:
    explicit Lexer(std::string text) : m_text(std::move(text)) {}

    bool Next(std::string& token)
    {
        Skip();
        if (m_pos >= m_text.size())
            return false;

        char c = m_text[m_pos++];
        if (c == '{' || c == '}')
        {
            token.assign(1, c);
            return true;
        }

        token.clear();
        if (c == '"')
        {
            while (m_pos < m_text.size())
            {
                c = m_text[m_pos++];
                if (c == '"')
                    return true;
                if (c == '\\' && m_pos < m_text.size())
                {
                    char escaped = m_text[m_pos++];
                    if (escaped == '"' || escaped == '\\')
                        token.push_back(escaped);
                    else
                    {
                        token.push_back('\\');
                        token.push_back(escaped);
                    }
                }
                else
                    token.push_back(c);
            }
            m_error = "unterminated quoted string";
            return false;
        }

        token.push_back(c);
        while (m_pos < m_text.size())
        {
            c = m_text[m_pos];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}')
                break;
            if (c == '/' && m_pos + 1 < m_text.size() && (m_text[m_pos + 1] == '/' || m_text[m_pos + 1] == '*'))
                break;
            token.push_back(c);
            m_pos++;
        }
        return true;
    }

    const std::string& Error() const { return m_error; }

private:
    void Skip()
    {
        for (;;)
        {
            while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])))
                m_pos++;
            if (m_pos + 1 >= m_text.size() || m_text[m_pos] != '/')
                return;
            if (m_text[m_pos + 1] == '/')
            {
                size_t end = m_text.find('\n', m_pos + 2);
                m_pos = end == std::string::npos ? m_text.size() : end + 1;
            }
            else if (m_text[m_pos + 1] == '*')
            {
                size_t end = m_text.find("*/", m_pos + 2);
                m_pos = end == std::string::npos ? m_text.size() : end + 2;
            }
            else
                return;
        }
    }

    std::string m_text;
    size_t m_pos = 0;
    std::string m_error;
};

bool ParseObject(Lexer& lexer, Node& node, bool nested, std::string& error)
{
    std::string key;
    while (lexer.Next(key))
    {
        if (key == "}")
        {
            if (!nested)
            {
                error = "unexpected closing brace";
                return false;
            }
            return true;
        }
        if (key == "{")
        {
            error = "expected a key before opening brace";
            return false;
        }

        std::string value;
        if (!lexer.Next(value))
        {
            error = lexer.Error().empty() ? "missing value" : lexer.Error();
            return false;
        }
        const std::string normalized = Lower(key);
        if (value == "{")
        {
            Node child;
            if (!ParseObject(lexer, child, true, error))
                return false;
            node.sections[normalized] = std::make_unique<Node>(std::move(child));
        }
        else if (value == "}")
        {
            error = "missing value before closing brace";
            return false;
        }
        else
            node.values[normalized] = std::move(value);
    }

    if (!lexer.Error().empty())
    {
        error = lexer.Error();
        return false;
    }
    if (nested)
    {
        error = "missing closing brace";
        return false;
    }
    return true;
}

std::string Get(const Node& node, const char* key)
{
    auto it = node.values.find(key);
    return it == node.values.end() ? std::string() : it->second;
}
}

bool ReadDatabaseConfig(const std::string& base, std::string_view name, sql::Config& cfg, std::string& error)
{
    const auto file = std::filesystem::path(base) / "addons/configs/databases.cfg";
    std::ifstream input(file, std::ios::binary);
    if (!input)
    {
        error = "could not open " + file.string();
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);

    Lexer lexer(std::move(text));
    Node root;
    if (!ParseObject(lexer, root, false, error))
    {
        error = "invalid databases.cfg: " + error;
        return false;
    }

    auto databases = root.sections.find("databases");
    if (databases == root.sections.end())
    {
        error = "databases.cfg has no Databases section";
        return false;
    }
    auto entry = databases->second->sections.find(Lower(std::string(name)));
    if (entry == databases->second->sections.end())
    {
        error = "database entry not found: " + std::string(name);
        return false;
    }

    std::string driver = Lower(Get(*entry->second, "driver"));
    if (driver.empty() || driver == "default")
        driver = Lower(Get(*databases->second, "driver_default"));
    if (driver == "mysql")
        cfg.kind = sql::Kind::MySQL;
    else if (driver == "pgsql" || driver == "postgres" || driver == "postgresql")
        cfg.kind = sql::Kind::PostgreSQL;
    else if (driver == "sqlite")
        cfg.kind = sql::Kind::SQLite;
    else
    {
        error = "unsupported database driver: " + driver;
        return false;
    }

    cfg.host = Get(*entry->second, "host");
    cfg.user = Get(*entry->second, "user");
    cfg.pass = Get(*entry->second, "pass");
    cfg.name = Get(*entry->second, "database");
    if (cfg.name.empty())
    {
        error = "database entry has no database field: " + std::string(name);
        return false;
    }

    const std::string port = Get(*entry->second, "port");
    if (!port.empty())
    {
        char* end = nullptr;
        const long value = std::strtol(port.c_str(), &end, 10);
        if (end == port.c_str() || *end != '\0' || value < 0 || value > 65535)
        {
            error = "invalid port in database entry: " + std::string(name);
            return false;
        }
        cfg.port = static_cast<int>(value);
    }

    const std::string timeout = Get(*entry->second, "timeout");
    if (!timeout.empty())
    {
        char* end = nullptr;
        const long value = std::strtol(timeout.c_str(), &end, 10);
        if (end != timeout.c_str() && *end == '\0' && value > 0 && value <= 86400)
            cfg.timeout = static_cast<int>(value);
    }

    const std::string charset = Get(*entry->second, "charset");
    if (!charset.empty())
        cfg.charset = charset;

    return true;
}
