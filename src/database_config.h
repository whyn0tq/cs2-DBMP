#pragma once

#include <sql.h>

#include <string>
#include <string_view>

bool ReadDatabaseConfig(const std::string& base, std::string_view name, sql::Config& cfg, std::string& error);
