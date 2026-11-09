#pragma once

#include "dispatcher.h"
#include "registry.h"
#include "database_config.h"

#include <functional>
#include <sql.h>
#include <string>

class Api final : public sql::IApiV2
{
public:
    using Owner = std::function<int(const void* caller)>;

    Api(Registry& conns, Dispatcher& out, std::string base, Owner owner);

    sql::IConn* Open(const sql::Config& cfg) override;
    sql::IConn* Open(std::string_view database) override;
    const char* Version() const override;

private:
    sql::IConn* OpenConfig(const sql::Config& cfg, int owner);
    std::string Resolve(const std::string& path) const;

    Registry& m_conns;
    Dispatcher& m_out;
    std::string m_base;
    Owner m_owner;
};
