#include "thread_pool.h"

#include <cassert>


ThreadPool::ThreadPool(size_t num_threads) {
    assert(num_threads > 0);
    threads_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this] { worker(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    not_empty_.notify_all();
    for (std::thread &t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPool::queue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(std::move(job));
    }
    not_empty_.notify_one();
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            // ждём условие: очередь не пуста (или поступила команда остановки)
            not_empty_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job();
    }
}
