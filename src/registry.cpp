#include "registry.h"

#include <algorithm>

void Registry::Add(std::shared_ptr<Conn> conn)
{
    m_conns.push_back(std::move(conn));
}

void Registry::Drop(int owner)
{
    for (auto& conn : m_conns)
    {
        if (conn->Owner() == owner)
            conn->Abandon();
    }
}

void Registry::Reap()
{
    auto done = [](const std::shared_ptr<Conn>& conn) { return conn->Done(); };
    m_conns.erase(std::remove_if(m_conns.begin(), m_conns.end(), done), m_conns.end());
}

void Registry::Shutdown()
{
    for (auto& conn : m_conns)
        conn->Close();
    for (auto& conn : m_conns)
        conn->Join();
    m_conns.clear();
}
