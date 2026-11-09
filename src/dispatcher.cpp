#include "dispatcher.h"

void Dispatcher::Post(Task task)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_tasks.push_back(std::move(task));
}

void Dispatcher::Run()
{
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        if (m_tasks.empty())
            return;
        tasks.swap(m_tasks);
    }

    for (auto& task : tasks)
        task();
}

void Dispatcher::Clear()
{
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        tasks.swap(m_tasks);
    }
}
