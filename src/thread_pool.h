#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>

class ThreadPool {

public:
    /**
     *  @param threads #threads
     */
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());

    ~ThreadPool();


    /**
     * @param f function, lambda
     * @param args function args
     * @return std::future<type>
     * 
     * example:
     * auto result = pool.enqueue([](int x) {return x*2}, 21);
     * std::cout << result.get() << std::endl; // output: 42
     */

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type>;

    size_t size() const {return workers.size();}


private:
    std::vector<std::thread> workers;

    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

};

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    ->std::future<typename std::invoke_result<F, Args...>::type>{
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            if(stop){
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks.emplace([task](){(*task)();});
        }

        condition.notify_one();

        return res;

    }

#endif