#pragma once

#include "backend/backend.h"
#include "dispatcher.h"
#include "worker.h"

#include <cstdint>
#include <memory>
#include <sql.h>
#include <unordered_map>

class Conn final : public sql::IConn, public std::enable_shared_from_this<Conn>
{
public:
    Conn(int owner, sql::Kind kind, std::unique_ptr<IBackend> backend, Dispatcher& out);
    ~Conn();

    void Start();
    void Connect(sql::OnConnect cb) override;
    void Query(std::string sql, sql::Args args, sql::OnResult cb) override;
    void Commit(sql::Tx tx, sql::OnCommit cb) override;
    std::string Escape(std::string_view s) const override;
    sql::Kind Type() const override { return m_kind; }
    void Close() override;

    void Abandon();
    int Owner() const { return m_owner; }
    bool Done() const { return m_worker.Done(); }
    void Join() { m_worker.Join(); }

private:
    std::vector<sql::Result> Run(const sql::Tx& tx, bool& ok);
    void Reply(uint64_t id, const std::string& sql, sql::Result r);
    void Reply(uint64_t id, bool ok, std::vector<sql::Result> results);

    int m_owner;
    sql::Kind m_kind;
    std::unique_ptr<IBackend> m_backend;
    Dispatcher& m_out;
    Worker m_worker;
    bool m_closed = false;
    uint64_t m_next = 0;
    std::unordered_map<uint64_t, sql::OnConnect> m_connects;
    std::unordered_map<uint64_t, sql::OnResult> m_results;
    std::unordered_map<uint64_t, sql::OnCommit> m_commits;
};
