#include"threadpool.h"
ThreadPool::ThreadPool(size_t numThreads) : stopFlag(false) {
    // 创建指定数量的线程
    for (size_t i = 0; i < numThreads; ++i) {//这会开启numThreads个线程同时执行worker函数，这些线程会共同访问上面的成员，不要和多进程搞混了。
        workers.emplace_back(&ThreadPool::worker, this);//必须绑定对象，因为函数worker是对象的内容
    }//构造函数直接开启所有的工作线程
}

ThreadPool::~ThreadPool() {
    stop();  // 停止线程池
}

void ThreadPool::addTask(std::function<void()> task) {//调用此函数后，worker函数才会被唤醒
    {
        std::lock_guard<std::mutex> lock(tasksMutex);  // 保护任务队列，防止可能多个函数共同修改了任务队列
        tasks.push(task);  // 将任务添加到队列
    }
    condition.notify_one();  // 通知一个等待的线程来处理任务
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(tasksMutex);//保护stopflag
        stopFlag = true;
    }
    condition.notify_all();  // 通知所有线程退出
    for (auto& worker : workers) {
        if (worker.joinable()) {//joinable不会阻塞，这是防止线程已经detach
            worker.join();  // 等待所有线程完成，会阻塞等待
        }
    }
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(tasksMutex);
            condition.wait(lock, [this] { return stopFlag || !tasks.empty(); });
    
            if (stopFlag && tasks.empty()) {
                return;  
            }
            
            task = std::move(tasks.front()); 
            tasks.pop();  
        }

        task();  
    }
}
