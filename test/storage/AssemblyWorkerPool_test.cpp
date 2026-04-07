/**
 * @file AssemblyWorkerPool_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief AssemblyWorkerPool 饱和、单飞、SlotGuard 与并发测试
 * @copyright Copyright (c) 2026
 */

#include "../../src/storage/AssemblyWorkerPool.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace disk::storage {
    namespace {

        class TestablePool {
        public:
            explicit TestablePool(size_t max_concurrent = AssemblyWorkerPool::DEFAULT_MAX_CONCURRENT)
                : m_max_concurrent(max_concurrent) {}

            auto TryAcquire(const std::string& upload_id) -> bool {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_active_upload_ids.contains(upload_id)) {
                    return false;
                }

                if (m_running >= m_max_concurrent) {
                    return false;
                }

                ++m_running;
                m_active_upload_ids.insert(upload_id);
                return true;
            }

            auto Release(const std::string& upload_id) -> void {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_active_upload_ids.erase(upload_id) == 0) {
                    return;
                }

                if (m_running > 0) {
                    --m_running;
                }
            }

            auto ActiveCount() const -> size_t {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_running;
            }

            auto RunningCount() const -> size_t {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_running;
            }

            auto MaxConcurrent() const -> size_t {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_max_concurrent;
            }

            auto IsUploadActive(const std::string& upload_id) const -> bool {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_active_upload_ids.contains(upload_id);
            }

        private:
            mutable std::mutex m_mutex;
            size_t m_max_concurrent = AssemblyWorkerPool::DEFAULT_MAX_CONCURRENT;
            size_t m_running = 0;
            std::unordered_set<std::string> m_active_upload_ids;
        };

        auto MakeUploadId(size_t index) -> std::string {
            return "upload-" + std::to_string(index);
        }

        auto DrainPool(TestablePool& pool, const std::vector<std::string>& upload_ids) -> void {
            for (const auto& upload_id : upload_ids) {
                pool.Release(upload_id);
            }
        }

        TEST(AssemblyWorkerPoolSaturation, FillRunningThenOverflow) {
            TestablePool pool;
            std::vector<std::string> upload_ids;
            const auto max_concurrent = pool.MaxConcurrent();

            for (size_t i = 0; i < max_concurrent; ++i) {
                auto upload_id = MakeUploadId(i);
                upload_ids.push_back(upload_id);
                EXPECT_TRUE(pool.TryAcquire(upload_id)) << "running slot " << i;
            }

            EXPECT_EQ(pool.RunningCount(), max_concurrent);
            EXPECT_EQ(pool.ActiveCount(), max_concurrent);
            EXPECT_FALSE(pool.TryAcquire("overflow-upload"));
            EXPECT_FALSE(pool.TryAcquire("overflow-upload-2"));

            DrainPool(pool, upload_ids);
            EXPECT_EQ(pool.ActiveCount(), 0U);
        }

        TEST(AssemblyWorkerPoolSaturation, SameUploadIdIsRejectedBeforeSaturation) {
            TestablePool pool;

            ASSERT_TRUE(pool.TryAcquire("same-upload"));
            EXPECT_FALSE(pool.TryAcquire("same-upload"));
            EXPECT_EQ(pool.ActiveCount(), 1U);
            EXPECT_TRUE(pool.IsUploadActive("same-upload"));

            pool.Release("same-upload");
            EXPECT_EQ(pool.ActiveCount(), 0U);
            EXPECT_FALSE(pool.IsUploadActive("same-upload"));
        }

        TEST(AssemblyWorkerPoolRelease, ReleaseRestoresSlot) {
            TestablePool pool;
            std::vector<std::string> upload_ids;
            const auto max_concurrent = pool.MaxConcurrent();

            for (size_t i = 0; i < max_concurrent; ++i) {
                auto upload_id = MakeUploadId(i + 100);
                upload_ids.push_back(upload_id);
                ASSERT_TRUE(pool.TryAcquire(upload_id));
            }
            ASSERT_FALSE(pool.TryAcquire("overflow-upload"));

            pool.Release(upload_ids.front());
            EXPECT_EQ(pool.ActiveCount(), max_concurrent - 1);
            EXPECT_FALSE(pool.IsUploadActive(upload_ids.front()));

            ASSERT_TRUE(pool.TryAcquire("replacement-upload"));
            EXPECT_EQ(pool.ActiveCount(), max_concurrent);
            EXPECT_FALSE(pool.TryAcquire("replacement-upload-2"));

            for (size_t i = 1; i < upload_ids.size(); ++i) {
                pool.Release(upload_ids[i]);
            }
            pool.Release("replacement-upload");
        }

        TEST(AssemblyWorkerPoolRelease, ReleaseClearsSingleflightState) {
            TestablePool pool;

            ASSERT_TRUE(pool.TryAcquire("upload-a"));
            EXPECT_TRUE(pool.IsUploadActive("upload-a"));

            pool.Release("upload-a");
            EXPECT_FALSE(pool.IsUploadActive("upload-a"));
            EXPECT_TRUE(pool.TryAcquire("upload-a"));

            pool.Release("upload-a");
        }

        TEST(AssemblyWorkerPoolSlotGuard, ReleasesOnScopeExit) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const std::string upload_id = "slot-guard-scope";

            {
                auto guard = pool.TryAcquireGuard(upload_id);
                ASSERT_TRUE(guard.has_value());
                EXPECT_EQ(pool.ActiveCount(), baseline + 1);
                EXPECT_TRUE(pool.IsUploadActive(upload_id));
            }

            EXPECT_EQ(pool.ActiveCount(), baseline);
            EXPECT_FALSE(pool.IsUploadActive(upload_id));
        }

        TEST(AssemblyWorkerPoolSlotGuard, MoveDoesNotLeakSlots) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const std::string upload_id = "slot-guard-move";

            {
                auto guard1 = pool.TryAcquireGuard(upload_id);
                ASSERT_TRUE(guard1.has_value());
                EXPECT_EQ(pool.ActiveCount(), baseline + 1);

                auto guard2 = std::move(*guard1);
                EXPECT_EQ(pool.ActiveCount(), baseline + 1);
                EXPECT_TRUE(pool.IsUploadActive(upload_id));
            }

            EXPECT_EQ(pool.ActiveCount(), baseline);
            EXPECT_FALSE(pool.IsUploadActive(upload_id));
        }

        TEST(AssemblyWorkerPoolSlotGuard, SaturatedReturnsNullopt) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const auto max_concurrent = pool.MaxConcurrent();
            ASSERT_GE(max_concurrent, baseline);
            const auto available_slots = max_concurrent - baseline;

            std::vector<AssemblyWorkerPool::SlotGuard> guards;
            for (size_t i = 0; i < available_slots; ++i) {
                auto guard = pool.TryAcquireGuard(MakeUploadId(i + 1000));
                ASSERT_TRUE(guard.has_value()) << "slot " << i;
                guards.push_back(std::move(*guard));
            }

            EXPECT_EQ(pool.ActiveCount(), max_concurrent);
            EXPECT_FALSE(pool.TryAcquireGuard("overflow-upload").has_value());
        }

        TEST(AssemblyWorkerPoolSlotGuard, DuplicateUploadIdReturnsNullopt) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const std::string upload_id = "slot-guard-duplicate";

            auto guard = pool.TryAcquireGuard(upload_id);
            ASSERT_TRUE(guard.has_value());
            EXPECT_FALSE(pool.TryAcquireGuard(upload_id).has_value());
            EXPECT_EQ(pool.ActiveCount(), baseline + 1);
        }

        TEST(AssemblyWorkerPoolConcurrency, ConcurrentAcquireRelease) {
            TestablePool pool;
            constexpr int iterations = 200;
            constexpr int thread_count = 8;
            std::atomic<int> successes{ 0 };
            std::atomic<int> rejections{ 0 };

            auto worker = [&](int thread_index) {
                for (int i = 0; i < iterations; ++i) {
                    const auto upload_id =
                        "thread-" + std::to_string(thread_index) + "-upload-" + std::to_string(i);
                    if (pool.TryAcquire(upload_id)) {
                        successes.fetch_add(1, std::memory_order_relaxed);
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                        pool.Release(upload_id);
                    } else {
                        rejections.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            };

            std::vector<std::thread> threads;
            threads.reserve(thread_count);
            for (int t = 0; t < thread_count; ++t) {
                threads.emplace_back(worker, t);
            }
            for (auto& thread : threads) {
                thread.join();
            }

            EXPECT_EQ(successes + rejections, thread_count * iterations);
            EXPECT_EQ(pool.ActiveCount(), 0U);
        }

        TEST(AssemblyWorkerPoolBoundary, ReleaseOnEmptyIsHarmless) {
            TestablePool pool;

            pool.Release("missing-upload");
            pool.Release("missing-upload-2");

            EXPECT_EQ(pool.ActiveCount(), 0U);
            EXPECT_EQ(pool.RunningCount(), 0U);
            EXPECT_FALSE(pool.IsUploadActive("missing-upload"));
        }

        TEST(AssemblyWorkerPoolBoundary, ExactCapacityThenOverflow) {
            TestablePool pool;
            std::vector<std::string> upload_ids;
            const auto max_concurrent = pool.MaxConcurrent();

            for (size_t i = 0; i < max_concurrent; ++i) {
                auto upload_id = MakeUploadId(i + 2000);
                upload_ids.push_back(upload_id);
                ASSERT_TRUE(pool.TryAcquire(upload_id)) << "slot " << i;
            }

            EXPECT_FALSE(pool.TryAcquire("overflow-upload"));

            DrainPool(pool, upload_ids);
        }

        TEST(AssemblyWorkerPoolSingleton, GetInstanceReturnsSameObject) {
            auto& a = AssemblyWorkerPool::GetInstance();
            auto& b = AssemblyWorkerPool::GetInstance();
            EXPECT_EQ(&a, &b);
        }

    } // namespace
} // namespace disk::storage
