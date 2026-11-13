#include "thread_pool.hpp"
#include <stdexcept>

ThreadPool::ThreadPool(std::size_t workers, std::size_t capacity) : capacity_(capacity) {
    if (workers == 0 || capacity == 0) throw std::invalid_argument("workers/capacity must be positive");
    for (std::size_t i = 0; i < workers; ++i) workers_.emplace_back(&ThreadPool::worker_loop, this);
}
ThreadPool::~ThreadPool() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    ready_.notify_all(); space_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}
bool ThreadPool::submit(std::function<void()> job) {
    std::unique_lock<std::mutex> lock(mutex_);
    space_.wait(lock, [this] { return stopping_ || jobs_.size() < capacity_; });
    if (stopping_) return false;
    jobs_.push(std::move(job));
    ready_.notify_one();
    return true;
}
void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front()); jobs_.pop();
            space_.notify_one();
        }
        try { job(); } catch (...) { /* A client task must not kill a worker. */ }
    }
}
