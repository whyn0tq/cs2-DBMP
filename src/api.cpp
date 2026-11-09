#include "api.h"
#include "info.h"
#include "log.h"

#include <filesystem>

Api::Api(Registry& conns, Dispatcher& out, std::string base, Owner owner)
    : m_conns(conns), m_out(out), m_base(std::move(base)), m_owner(std::move(owner))
{
}

__attribute__((noinline)) sql::IConn* Api::Open(const sql::Config& cfg)
{
    int owner = m_owner ? m_owner(__builtin_return_address(0)) : 0;
    if (!owner)
        Log("Could not detect the plugin that opened a connection, it must call Close() before unloading");

    return OpenConfig(cfg, owner);
}

__attribute__((noinline)) sql::IConn* Api::Open(std::string_view database)
{
    int owner = m_owner ? m_owner(__builtin_return_address(0)) : 0;
    if (!owner)
        Log("Could not detect the plugin that opened a connection, it must call Close() before unloading");

    sql::Config cfg;
    std::string error;
    if (!ReadDatabaseConfig(m_base, database, cfg, error))
    {
        Log("Could not load database '%s' from databases.cfg: %s", std::string(database).c_str(), error.c_str());
        return nullptr;
    }

    return OpenConfig(cfg, owner);
}

sql::IConn* Api::OpenConfig(const sql::Config& cfg, int owner)
{

    sql::Config copy = cfg;
    if (copy.kind == sql::Kind::SQLite)
        copy.name = Resolve(copy.name);

    auto backend = MakeBackend(copy);
    if (!backend)
        return nullptr;

    auto conn = std::make_shared<Conn>(owner, copy.kind, std::move(backend), m_out);
    conn->Start();
    m_conns.Add(conn);
    return conn.get();
}

const char* Api::Version() const
{
    return PLUGIN_VERSION;
}

std::string Api::Resolve(const std::string& path) const
{
    if (path.empty() || path == ":memory:")
        return ":memory:";

    std::filesystem::path p(path);
    if (p.is_absolute())
        return path;
    return (std::filesystem::path(m_base) / p).lexically_normal().string();
}
