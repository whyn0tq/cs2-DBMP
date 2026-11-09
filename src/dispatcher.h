#pragma once

#include <functional>
#include <mutex>
#include <vector>

class Dispatcher
{
public:
    using Task = std::function<void()>;

    void Post(Task task);
    void Run();
    void Clear();

private:
    std::mutex m_lock;
    std::vector<Task> m_tasks;
};
