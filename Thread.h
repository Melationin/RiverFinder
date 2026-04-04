//
// Created by zhdds on 2026/1/20.
//

#ifndef CUBIOMES_THREAD_H
#define CUBIOMES_THREAD_H
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <iostream>

template<typename T>
class ThreadSafeResults {
    std::vector<T> results_;
    std::mutex mutex_{};


public:
    void addResult(const T &res)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(res);
        clean();
    }

    void addResults(const std::vector<T> &newResults)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        T new_max = *std::max_element(newResults.cbegin(), newResults.cend());
        results_.insert(results_.end(), newResults.cbegin(), newResults.cend());
        clean();
    }


    int size() const
    {
        return results_.size();
    }

    void clean()
    {
        if (results_.size() > 4000)
        {
             std::nth_element(results_.begin(),
                     results_.begin() + 1000,
                     results_.end(),
                     std::greater<T>());        // 注意：greater 使最大的排在前面
            results_.resize(1000);
        }
    }
    void clear()
    {
        results_.clear();
    }
    [[nodiscard]] bool empty() const
    {
        return results_.empty();
    }

    std::vector<T> getAllResults()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ranges::sort(results_,std::greater<T>());
        return results_;
    }
};

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()> > tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};

public:
    explicit ThreadPool(size_t threads)
    {
        for (size_t i = 0; i < threads; ++i)
        {
            workers.emplace_back([this] {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F &&f)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool()
    {
        stop = true;
        condition.notify_all();
        for (std::thread &worker: workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }
};
#endif //CUBIOMES_THREAD_H
