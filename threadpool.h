#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <thread>
#include <future>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <iostream>

class ThreadPool
{
private:
    std::vector<std::thread> workers;
    std::mutex qlock;
    std::condition_variable cv_completion;
    std::condition_variable cv_workers;
    std::atomic<size_t> tasks_in_progress;
    std::queue<std::function<void()>> tasks;
    std::atomic<bool> stop;

    void worker_function(void);

public:
    ThreadPool(size_t nthreads);
    ~ThreadPool();
    
    template <typename F, typename... Args>
    void enqueue(F&& f, Args&&... args);
    void wait_for_completion();
};

ThreadPool::ThreadPool(size_t nthreads) : stop(false), tasks_in_progress(0)
{
    for(size_t i = 0; i < nthreads; i++) {
        workers.emplace_back(&ThreadPool::worker_function, this);
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(qlock);
        stop = true;
    }
    cv_workers.notify_all();
    for(std::thread& worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::worker_function(void)
{
    while(1) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(qlock);
            while(!stop && tasks.empty()) {
                cv_workers.wait(lock);
            }
            if(stop && tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
            tasks_in_progress++;
        }
        try {
            task();
        }
        catch(const std::exception& e) {
            std::cerr << "Exception in thread pool task: " << e.what() << "\n";
        }
        catch(...) {
            std::cerr << "Unknown Exception in thread pool task\n";
        }
        {
            std::unique_lock<std::mutex> lock(qlock);
            tasks_in_progress--;
            if(tasks.empty() && tasks_in_progress == 0) {
                cv_completion.notify_all();
            }
        }
    }
}

void ThreadPool::wait_for_completion()
{
    std::unique_lock<std::mutex> lock(qlock);
    while(!tasks.empty() || tasks_in_progress != 0) {
        cv_completion.wait(lock);
    }
}

// template <typename F, typename... Args>
// auto ThreadPool::enqueue(F&& f, Args&&... args)
// {
//     using return_type = typename std::invoke_result<F, Args...>::type;
//     auto task = std::make_shared<std::packaged_task<returnType()>>(
//         std::bind(std::forward<F>(f), std::forward<Args>(args)...));
//     std::future<return_type> future = task->get_future();
//     {
//         std::unique_lock<std::mutex> lock(qlock);
//         task.emplace([task]() { (*task)(); })
//     }

//     cv_workers.notify_one();
//     return future;
// }

template <typename F, typename... Args>
void ThreadPool::enqueue(F&& f, Args&&... args)
{
    auto task = [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable {
        std::invoke(f, args...);
        };
    {
        std::unique_lock<std::mutex> lock(qlock);
        tasks.push(std::move(task));
    }
    cv_workers.notify_one();
}


#endif //__THREADPOOL_H__