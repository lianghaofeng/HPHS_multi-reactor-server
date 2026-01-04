#ifndef WORK_STEALING_POOL_H
#define WORK_STEALING_POOL_H

#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <future>
#include <functional>

class WorkStealingPool {
public:
    // 显式构造函数
    explicit WorkStealingPool(size_t threads = std::thread::hardware_concurrency());

    // 析构函数
    ~WorkStealingPool();

    // 提交任务
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // 获取线程池大小
    size_t size() const {return workers_.size();}

private:
    // 每个线程的工作队列
    struct WorkQueue{
        std::deque<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<size_t> as{0};
    };

    // 工作线程函数
    void worker_thread(size_t thread_id);

    // 本地队列取出任务
    bool pop_local_task(size_t thread_id, std::function<void()> &task); 

    // 从其他队列偷任务
    bool steal_task(size_t thief_id, std::function<void()> &task);

    // 添加任务到指定线程队列
    void push_task(size_t thread_id, std::function<void()> task);

    //每个线程的队列
    std::vector<std::unique_ptr<WorkQueue>> queues_;
    std::vector<std::thread> workers_;
    std::atomic<size_t> next_queue_;
    std::atomic<bool> stop_;

};


template<class F, class... Args>
auto WorkStealingPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>{
    
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();

    // 选择队列
    size_t queue_id = next_queue_++ % queues_.size();

    // 添加到指定队列
    push_task(queue_id, [task](){(*task)();});
    
    return res;

}


#endif