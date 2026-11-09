#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class Worker
{
public:
    using Job = std::function<void()>;

    ~Worker();

    void Start(Job enter, Job leave);
    void Push(Job job);
    void Stop();
    void Join();
    bool Done() const { return m_done; }

private:
    void Loop(Job enter, Job leave);

    std::thread m_thread;
    std::mutex m_lock;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    bool m_stop = false;
    std::atomic<bool> m_done{false};
};
