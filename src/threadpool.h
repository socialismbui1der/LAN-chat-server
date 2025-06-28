#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    void addTask(std::function<void()> task);
    void stop();

private:
    std::vector<std::thread> workers;           // 工作线程
    std::queue<std::function<void()>> tasks;    // 任务队列
    std::mutex tasksMutex;                      // 任务队列的互斥锁
    std::condition_variable condition;          // 条件变量，用于线程间同步
    bool stopFlag;                              // 停止标志
    void worker();
};
