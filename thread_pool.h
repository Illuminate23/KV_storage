#pragma once

#include <cstddef>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>


// Пул из фиксированного числа рабочих потоков, разбирающих очередь задач (FIFO).
// Используется, чтобы вынести освобождение больших контейнеров с потока
// главного цикла обработки событий.
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    void queue(std::function<void()> job);

private:
    std::vector<std::thread> threads_;
    std::deque<std::function<void()>> queue_;
    std::mutex mu_;
    std::condition_variable not_empty_;
    bool stop_ = false;

    void worker();
};
