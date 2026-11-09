#include "worker.h"

Worker::~Worker()
{
    Stop();
    Join();
}

void Worker::Start(Job enter, Job leave)
{
    m_thread = std::thread(&Worker::Loop, this, std::move(enter), std::move(leave));
}

void Worker::Push(Job job)
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        if (m_stop)
            return;
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void Worker::Stop()
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_stop = true;
    }
    m_wake.notify_one();
}

void Worker::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void Worker::Loop(Job enter, Job leave)
{
    if (enter)
        enter();

    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> guard(m_lock);
            m_wake.wait(guard, [this] { return m_stop || !m_jobs.empty(); });
            if (m_jobs.empty())
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        job();
    }

    if (leave)
        leave();

    m_done = true;
}
