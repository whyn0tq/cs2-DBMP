#pragma once

#include "conn.h"

#include <memory>
#include <vector>

class Registry
{
public:
    void Add(std::shared_ptr<Conn> conn);
    void Drop(int owner);
    void Reap();
    void Shutdown();
    size_t Size() const { return m_conns.size(); }

private:
    std::vector<std::shared_ptr<Conn>> m_conns;
};
