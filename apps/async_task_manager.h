#pragma once

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D {

struct TaskBase {
    enum class Status : uint8_t { Pending, Running, Completed, Failed };

    std::string id;
    Status status = Status::Pending;
    std::string error_message;

    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point completed_at;
};

inline const char* TaskStatusToString(TaskBase::Status s) {
    switch (s) {
    case TaskBase::Status::Pending:   return "pending";
    case TaskBase::Status::Running:   return "running";
    case TaskBase::Status::Completed: return "completed";
    case TaskBase::Status::Failed:    return "failed";
    }
    return "unknown";
}

// Generic asynchronous task manager.
// TInfo must derive from TaskBase and carry domain-specific fields/results.
// All thread management, concurrency control, and TTL cleanup are handled here.
template<std::derived_from<TaskBase> TInfo>
class AsyncTaskManager {
public:
    // Thread-safe modifier: call with a lambda that mutates TInfo& under lock.
    using UpdateFn = std::function<void(std::function<void(TInfo&)>)>;
    // Worker function: receives (task_id, update_fn).
    //   - Normal return  => manager sets status to Completed
    //   - Throw          => manager sets status to Failed with error message
    //   - Use update_fn to write results / progress into the task
    using WorkerFn = std::function<void(const std::string&, UpdateFn)>;

    explicit AsyncTaskManager(int max_concurrent = 4, int ttl_seconds = 600)
        : max_concurrent_(max_concurrent)
        , running_count_(0)
        , task_ttl_seconds_(ttl_seconds)
        , shutdown_(false) {
        cleanup_thread_ = std::thread([this]() { CleanupLoop(); });
    }

    ~AsyncTaskManager() {
        shutdown_.store(true);
        concurrency_cv_.notify_all();
        cleanup_cv_.notify_all();
        if (cleanup_thread_.joinable()) { cleanup_thread_.join(); }

        std::unique_lock<std::mutex> lock(mutex_);
        for (auto& w : worker_threads_) {
            lock.unlock();
            if (w.thread.joinable()) { w.thread.join(); }
            lock.lock();
        }
        worker_threads_.clear();
    }

    AsyncTaskManager(const AsyncTaskManager&)            = delete;
    AsyncTaskManager& operator=(const AsyncTaskManager&) = delete;

    // Submit a task. Returns the generated task ID.
    // `initial` carries domain-specific metadata (id/status/timestamps are set automatically).
    std::string Submit(TInfo initial, WorkerFn worker) {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanWorkerThreads();

        std::string id  = GenerateId();
        initial.id      = id;
        initial.status  = TaskBase::Status::Pending;
        initial.created_at = std::chrono::steady_clock::now();
        tasks_.emplace(id, std::move(initial));

        auto done = std::make_shared<std::atomic<bool>>(false);
        worker_threads_.push_back({
            std::thread([this, id, w = std::move(worker), done]() {
                RunTask(id, w);
                done->store(true, std::memory_order_release);
            }),
            done
        });

        return id;
    }

    std::optional<TInfo> GetTask(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return std::nullopt;
        return it->second;
    }

    bool DeleteTask(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return false;
        if (it->second.status == TaskBase::Status::Running) return false;
        tasks_.erase(it);
        return true;
    }

    // Run a reader/writer function on a task while holding the lock.
    // Returns false if the task does not exist.
    template<typename Fn>
    bool WithTask(const std::string& id, Fn&& fn) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return false;
        std::forward<Fn>(fn)(it->second);
        return true;
    }

    int ActiveTaskCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_count_;
    }

    int TotalTaskCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(tasks_.size());
    }

    // Iterate over all tasks under a single lock (avoids copying).
    template<typename Fn>
    void ForEachTask(Fn&& fn) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [tid, info] : tasks_) {
            std::forward<Fn>(fn)(info);
        }
    }

private:
    void RunTask(const std::string& id, const WorkerFn& worker) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            concurrency_cv_.wait(
                lock, [this]() { return running_count_ < max_concurrent_ || shutdown_.load(); });
            if (shutdown_.load()) return;

            auto it = tasks_.find(id);
            if (it == tasks_.end()) return;
            it->second.status = TaskBase::Status::Running;
            ++running_count_;
        }

        UpdateFn update = [this, &id](std::function<void(TInfo&)> fn) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(id);
            if (it != tasks_.end()) fn(it->second);
        };

        try {
            worker(id, update);
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(id);
            if (it != tasks_.end()) {
                it->second.status       = TaskBase::Status::Completed;
                it->second.completed_at = std::chrono::steady_clock::now();
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(id);
            if (it != tasks_.end()) {
                it->second.status        = TaskBase::Status::Failed;
                it->second.error_message = e.what();
                it->second.completed_at  = std::chrono::steady_clock::now();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(id);
            if (it != tasks_.end()) {
                it->second.status        = TaskBase::Status::Failed;
                it->second.error_message = "Unknown error";
                it->second.completed_at  = std::chrono::steady_clock::now();
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --running_count_;
        }
        concurrency_cv_.notify_one();
    }

    void CleanupLoop() {
        while (!shutdown_.load()) {
            std::unique_lock<std::mutex> lock(mutex_);
            cleanup_cv_.wait_for(lock, std::chrono::seconds(60),
                                 [this]() { return shutdown_.load(); });
            if (shutdown_.load()) return;

            auto now = std::chrono::steady_clock::now();
            auto ttl = std::chrono::seconds(task_ttl_seconds_);
            std::vector<std::string> expired;
            for (const auto& [tid, info] : tasks_) {
                if (info.status == TaskBase::Status::Completed ||
                    info.status == TaskBase::Status::Failed) {
                    if (now - info.completed_at > ttl) expired.push_back(tid);
                }
            }
            for (const auto& tid : expired) {
                spdlog::debug("AsyncTaskManager: cleaning expired task {}", tid.substr(0, 8));
                tasks_.erase(tid);
            }
        }
    }

    void CleanWorkerThreads() {
        auto it = std::remove_if(worker_threads_.begin(), worker_threads_.end(),
            [](WorkerEntry& w) {
                if (w.done->load(std::memory_order_acquire)) {
                    if (w.thread.joinable()) w.thread.join();
                    return true;
                }
                return false;
            });
        worker_threads_.erase(it, worker_threads_.end());
    }

    static std::string GenerateId() {
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<uint64_t> dist;
        uint64_t a = dist(gen);
        uint64_t b = dist(gen);

        static const char hex[] = "0123456789abcdef";
        std::string id;
        id.reserve(32);
        for (int i = 15; i >= 0; --i) id.push_back(hex[(a >> (i * 4)) & 0xf]);
        for (int i = 15; i >= 0; --i) id.push_back(hex[(b >> (i * 4)) & 0xf]);
        return id;
    }

    struct WorkerEntry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    mutable std::mutex mutex_;
    std::condition_variable concurrency_cv_;
    std::condition_variable cleanup_cv_;
    std::unordered_map<std::string, TInfo> tasks_;
    std::vector<WorkerEntry> worker_threads_;

    int max_concurrent_;
    int running_count_;
    int task_ttl_seconds_;
    std::atomic<bool> shutdown_;
    std::thread cleanup_thread_;
};

} // namespace ChromaPrint3D
