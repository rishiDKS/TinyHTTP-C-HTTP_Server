#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Fixed workers + bounded queue: limits thread creation and applies back-pressure.
class ThreadPool {
public:
    ThreadPool(std::size_t workers, std::size_t capacity);
    ~ThreadPool();
    bool submit(std::function<void()> job);
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    void worker_loop();
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable space_;
    std::size_t capacity_;
    bool stopping_ = false;
};
