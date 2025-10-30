#pragma once

#include <iostream>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <stack>

namespace MZ
{
    class SignalDispatcher
    {
    private:

        std::queue<uint32_t> queue;  // очередь id для обработки
        std::stack<uint32_t> pool;  // пул свободных id

        std::mutex mtx;

        std::condition_variable cv;

        std::thread thread;

        bool stopFlag = false;

        size_t poolSize;

        using CallBackType = std::function<void(uint32_t id)>;

        CallBackType threadCallBack;

    public:
        SignalDispatcher(const CallBackType& tcb, uint32_t ps) : threadCallBack(tcb), poolSize(ps)
        {
            for (uint32_t i = 0; i < poolSize; i++) pool.emplace(i);

            thread = std::thread([this]()
            {
                while (true)
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this] { return !queue.empty() || stopFlag; });

                    if (stopFlag && queue.empty()) return;

                    if (!queue.empty())
                    {
                        const auto id = queue.front(); queue.pop(); lock.unlock();

                        threadCallBack(id);

                        lock.lock(); pool.push(id); lock.unlock();

                        cv.notify_one(); // уведомляем, что id освободился
                    }
                }
            });
        }

        void Create(const CallBackType& signalCallBack)
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return !pool.empty(); }); // ждём, пока появится свободный id

            signalCallBack(pool.top());
            
            queue.push(pool.top()); pool.pop(); lock.unlock();

            cv.notify_one();
        }

        void Wait()
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return pool.size() == poolSize; });
        }

        void Shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(mtx);
                stopFlag = true;
            }

            cv.notify_all();

            if (thread.joinable()) thread.join();
        }

        ~SignalDispatcher()
        {
            Shutdown();
        }
    };
}
