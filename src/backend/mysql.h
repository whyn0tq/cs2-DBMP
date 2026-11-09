#pragma once

#include "backend.h"

#include <atomic>
#include <chrono>

struct st_mysql;

class MySql final : public IBackend
{
public:
    explicit MySql(sql::Config cfg);
    ~MySql() override;

    void Attach() override;
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
    sql::Result Send(const std::string& text);
    sql::Result Read();
    std::string Quote(std::string_view s) const;

    sql::Config m_cfg;
    st_mysql* m_db = nullptr;
    bool m_tx = false;
    std::atomic<bool> m_plain{false};
    std::chrono::steady_clock::time_point m_used;
};
