#include "conn.h"
#include "log.h"

Conn::Conn(int owner, sql::Kind kind, std::unique_ptr<IBackend> backend, Dispatcher& out)
    : m_owner(owner), m_kind(kind), m_backend(std::move(backend)), m_out(out)
{
}

Conn::~Conn()
{
    m_worker.Stop();
    m_worker.Join();
}

void Conn::Start()
{
    IBackend* backend = m_backend.get();
    m_worker.Start([backend] { backend->Attach(); }, [backend] { backend->Detach(); });
}

void Conn::Connect(sql::OnConnect cb)
{
    if (m_closed)
        return;

    uint64_t id = 0;
    if (cb)
        m_connects.emplace(id = ++m_next, std::move(cb));

    m_worker.Push([this, id] {
        std::string error;
        bool ok = m_backend->Ready(error);
        m_out.Post([self = shared_from_this(), id, ok, error] {
            auto it = self->m_connects.find(id);
            if (it == self->m_connects.end())
            {
                if (!id && !ok)
                    Log("Connection failed: %s", error.c_str());
                return;
            }

            sql::OnConnect cb = std::move(it->second);
            self->m_connects.erase(it);
            cb(ok, error);
        });
    });
}

void Conn::Query(std::string sql, sql::Args args, sql::OnResult cb)
{
    if (m_closed)
        return;

    uint64_t id = 0;
    if (cb)
        m_results.emplace(id = ++m_next, std::move(cb));

    m_worker.Push([this, id, sql = std::move(sql), args = std::move(args)] {
        Reply(id, sql, m_backend->Run(sql, args));
    });
}

void Conn::Commit(sql::Tx tx, sql::OnCommit cb)
{
    if (m_closed)
        return;

    uint64_t id = 0;
    if (cb)
        m_commits.emplace(id = ++m_next, std::move(cb));

    m_worker.Push([this, id, tx = std::move(tx)] {
        bool ok = false;
        auto results = Run(tx, ok);
        Reply(id, ok, std::move(results));
    });
}

std::string Conn::Escape(std::string_view s) const
{
    return m_backend->Escape(s);
}

void Conn::Close()
{
    if (m_closed)
        return;

    m_closed = true;
    m_connects.clear();
    m_results.clear();
    m_commits.clear();
    m_worker.Stop();
}

void Conn::Abandon()
{
    static_cast<void>(new decltype(m_connects)(std::move(m_connects)));
    static_cast<void>(new decltype(m_results)(std::move(m_results)));
    static_cast<void>(new decltype(m_commits)(std::move(m_commits)));
    Close();
}

std::vector<sql::Result> Conn::Run(const sql::Tx& tx, bool& ok)
{
    std::vector<sql::Result> results;
    sql::Result begin = m_backend->Begin();
    if (!begin.Ok())
    {
        ok = false;
        results.push_back(std::move(begin));
        return results;
    }

    ok = true;
    for (const auto& item : tx.Items())
    {
        results.push_back(m_backend->Run(item.sql, item.args));
        if (!results.back().Ok())
        {
            ok = false;
            break;
        }
    }

    if (ok)
    {
        sql::Result end = m_backend->Commit();
        if (end.Ok())
            return results;
        ok = false;
        results.push_back(std::move(end));
    }

    m_backend->Rollback();
    return results;
}

void Conn::Reply(uint64_t id, const std::string& sql, sql::Result r)
{
    std::string text = !id && !r.Ok() ? sql : std::string();
    m_out.Post([self = shared_from_this(), id, text = std::move(text), r = std::move(r)]() mutable {
        auto it = self->m_results.find(id);
        if (it == self->m_results.end())
        {
            if (!id && !r.Ok())
                Log("Query failed: %s | %s", r.Error().c_str(), text.c_str());
            return;
        }

        sql::OnResult cb = std::move(it->second);
        self->m_results.erase(it);
        cb(r);
    });
}

void Conn::Reply(uint64_t id, bool ok, std::vector<sql::Result> results)
{
    m_out.Post([self = shared_from_this(), id, ok, results = std::move(results)]() mutable {
        auto it = self->m_commits.find(id);
        if (it == self->m_commits.end())
        {
            if (!id && !ok && !results.empty())
                Log("Transaction failed: %s", results.back().Error().c_str());
            return;
        }

        sql::OnCommit cb = std::move(it->second);
        self->m_commits.erase(it);
        cb(ok, results);
    });
}
