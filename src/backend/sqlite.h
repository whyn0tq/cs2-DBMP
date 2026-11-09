#pragma once

#include "backend.h"

struct sqlite3;
struct sqlite3_stmt;

class Sqlite final : public IBackend
{
public:
    explicit Sqlite(sql::Config cfg);
    ~Sqlite() override;

    bool Ready(std::string& error) override;
    sql::Result Run(const std::string& text, const sql::Args& args) override;
    sql::Result Begin() override;
    sql::Result Commit() override;
    sql::Result Rollback() override;
    std::string Escape(std::string_view s) const override;
    void Close() override;

private:
    bool Open(std::string& error);
    bool Apply(sqlite3_stmt* stmt, const sql::Args& args, size_t& used, bool last, std::string& error);
    sql::Result Step(sqlite3_stmt* stmt);

    sql::Config m_cfg;
    sqlite3* m_db = nullptr;
};
