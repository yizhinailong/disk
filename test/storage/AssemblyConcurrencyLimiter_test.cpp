/**
 * @file AssemblyConcurrencyLimiter_test.cpp
 * @brief AssemblyConcurrencyLimiter 槽位、RAII 与并发上限测试
 * @copyright Copyright (c) 2026
 */

#include "../../src/storage/AssemblyConcurrencyLimiter.hpp"

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace disk::storage {
    namespace {

        template <typename T>
        concept AcceptsUploadIdentifier = requires(T& limiter) {
            limiter.TryAcquire("upload-id");
        };

        static_assert(!AcceptsUploadIdentifier<AssemblyConcurrencyLimiter>);

        TEST(AssemblyConcurrencyLimiterTest, GetInstanceReturnsSameObject) {
            auto& first = AssemblyConcurrencyLimiter::GetInstance();
            auto& second = AssemblyConcurrencyLimiter::GetInstance();

            EXPECT_EQ(&first, &second);
        }

        TEST(AssemblyConcurrencyLimiterTest, SlotIsReleasedOnScopeExit) {
            auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
            const auto baseline = limiter.RunningCount();
            ASSERT_LT(baseline, limiter.MaxConcurrent());

            {
                auto guard = limiter.TryAcquire();
                ASSERT_TRUE(guard.has_value());
                EXPECT_EQ(limiter.RunningCount(), baseline + 1);
            }

            EXPECT_EQ(limiter.RunningCount(), baseline);
        }

        TEST(AssemblyConcurrencyLimiterTest, MoveTransfersSlotOwnership) {
            auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
            const auto baseline = limiter.RunningCount();
            ASSERT_LT(baseline, limiter.MaxConcurrent());

            {
                auto guard = limiter.TryAcquire();
                ASSERT_TRUE(guard.has_value());

                auto moved_guard = std::move(*guard);
                guard.reset();
                EXPECT_EQ(limiter.RunningCount(), baseline + 1);
            }

            EXPECT_EQ(limiter.RunningCount(), baseline);
        }

        TEST(AssemblyConcurrencyLimiterTest, MoveAssignmentReleasesPreviousSlot) {
            auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
            const auto baseline = limiter.RunningCount();
            if (limiter.MaxConcurrent() - baseline < 2) {
                GTEST_SKIP() << "Move assignment requires two available slots";
            }

            auto first = limiter.TryAcquire();
            auto second = limiter.TryAcquire();
            ASSERT_TRUE(first.has_value());
            ASSERT_TRUE(second.has_value());
            ASSERT_EQ(limiter.RunningCount(), baseline + 2);

            *first = std::move(*second);
            second.reset();
            EXPECT_EQ(limiter.RunningCount(), baseline + 1);

            first.reset();
            EXPECT_EQ(limiter.RunningCount(), baseline);
        }

        TEST(AssemblyConcurrencyLimiterTest, CapacityRejectsOverflowAndRecovers) {
            auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
            const auto baseline = limiter.RunningCount();
            const auto max_concurrent = limiter.MaxConcurrent();
            ASSERT_LT(baseline, max_concurrent);

            std::vector<AssemblyConcurrencyLimiter::SlotGuard> guards;
            guards.reserve(max_concurrent - baseline);
            while (limiter.RunningCount() < max_concurrent) {
                auto guard = limiter.TryAcquire();
                ASSERT_TRUE(guard.has_value());
                guards.push_back(std::move(*guard));
            }

            EXPECT_FALSE(limiter.TryAcquire().has_value());
            guards.clear();
            EXPECT_EQ(limiter.RunningCount(), baseline);

            auto replacement = limiter.TryAcquire();
            ASSERT_TRUE(replacement.has_value());
            replacement.reset();
            EXPECT_EQ(limiter.RunningCount(), baseline);
        }

        TEST(AssemblyConcurrencyLimiterTest, ConcurrentAdmissionNeverExceedsCapacity) {
            auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
            const auto baseline = limiter.RunningCount();
            const auto max_concurrent = limiter.MaxConcurrent();
            ASSERT_LT(baseline, max_concurrent);

            const auto available_slots = max_concurrent - baseline;
            const auto worker_count = available_slots + 4;
            std::atomic<size_t> ready{ 0 };
            std::atomic<size_t> attempted{ 0 };
            std::atomic<size_t> acquired{ 0 };
            std::atomic<bool> start{ false };
            std::atomic<bool> release{ false };

            auto worker = [&]() {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                auto guard = limiter.TryAcquire();
                if (guard.has_value()) {
                    acquired.fetch_add(1, std::memory_order_relaxed);
                }
                attempted.fetch_add(1, std::memory_order_release);

                while (guard.has_value() && !release.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            };

            std::vector<std::thread> workers;
            workers.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) {
                workers.emplace_back(worker);
            }

            while (ready.load(std::memory_order_acquire) != worker_count) {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);
            while (attempted.load(std::memory_order_acquire) != worker_count) {
                std::this_thread::yield();
            }

            EXPECT_EQ(acquired.load(std::memory_order_relaxed), available_slots);
            EXPECT_EQ(limiter.RunningCount(), max_concurrent);

            release.store(true, std::memory_order_release);
            for (auto& thread : workers) {
                thread.join();
            }

            EXPECT_EQ(limiter.RunningCount(), baseline);
        }

    } // namespace
} // namespace disk::storage
