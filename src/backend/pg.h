#pragma once

#include "backend.h"

#include <chrono>

struct pg_conn;
struct pg_result;

class Pg final : public IBackend
{
public:
    explicit Pg(sql::Config cfg);
    ~Pg() override;

    void Detach() override;
    bool Ready(std::string& error) override;
    sql::Result Run(const std::string& text, const sql::Args& args) override;
    sql::Result Begin() override;
    sql::Result Commit() override;
    sql::Result Rollback() override;
    std::string Escape(std::string_view s) const override;
    void Close() override;

private:
    bool Connect(std::string& error);
    sql::Result End(const char* text);
    sql::Result Send(const std::string& text, const sql::Args& args);
    sql::Result Read(pg_result* res);

    sql::Config m_cfg;
    pg_conn* m_db = nullptr;
    bool m_tx = false;
    std::chrono::steady_clock::time_point m_used;
};
