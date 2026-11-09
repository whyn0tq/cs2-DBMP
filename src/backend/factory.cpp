#include "backend.h"
#include "mysql.h"
#include "pg.h"
#include "sqlite.h"

std::unique_ptr<IBackend> MakeBackend(const sql::Config& cfg)
{
    switch (cfg.kind)
    {
    case sql::Kind::MySQL:
        return std::make_unique<MySql>(cfg);
    case sql::Kind::SQLite:
        return std::make_unique<Sqlite>(cfg);
    case sql::Kind::PostgreSQL:
        return std::make_unique<Pg>(cfg);
    }
    return nullptr;
}
