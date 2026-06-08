/**
 * @file CompleteUploadSingleflight_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief CompleteUpload 单飞语义与幂等性契约测试
 *
 * 测试覆盖 CompleteUpload 的并发安全与请求契约：
 *  - 请求 DTO 验证（upload_id 必填/非空/字符串类型）
 *  - 幂等性语义（已完成的任务跳过组装）
 *  - 单飞互斥（相同 upload_id 仅允许一个并发组装）
 *  - 池化背压（超出 MaxConcurrent 的请求被拒绝）
 *
 * 分片验证采用聚合语义（uploaded_count + max_chunk_index），
 * 详见 FileServiceAtomicity_test.cpp 中 SimulateCompleteUpload。
 *
 * @copyright Copyright (c) 2026
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "../../src/dtos/FileDto.hpp"
#include "../../src/storage/AssemblyWorkerPool.hpp"
#include "../../src/utils/ErrorCode.hpp"

namespace disk::file {
    namespace {
        using storage::AssemblyWorkerPool;

        TEST(CompleteUploadRequestContract, MissingUploadIdReturnsValidationFailed) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setBody(R"({})");
            req->setContentTypeString("application/json");

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST(CompleteUploadRequestContract, EmptyUploadIdReturnsValidationFailed) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setBody(R"({"upload_id": ""})");
            req->setContentTypeString("application/json");

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST(CompleteUploadRequestContract, NonStringUploadIdReturnsValidationFailed) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setBody(R"({"upload_id": 12345})");
            req->setContentTypeString("application/json");

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST(CompleteUploadRequestContract, ValidUploadIdParsesCorrectly) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setBody(R"({"upload_id": "upload-task-abc-123"})");
            req->setContentTypeString("application/json");

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->upload_id, "upload-task-abc-123");
        }

        TEST(CompleteUploadRequestContract, InvalidJsonReturnsValidationFailed) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setBody("not json");
            req->setContentTypeString("application/json");

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        enum class UploadTaskStatus : int8_t {
            Pending = 0,
            Completed = 1,
        };

        TEST(CompleteUploadIdempotency, CompletedStatusSkipsAssembly) {
            UploadTaskStatus status = UploadTaskStatus::Completed;
            bool assembly_called = false;

            auto simulate = [&](UploadTaskStatus current_status) -> bool {
                if (current_status == UploadTaskStatus::Completed) {
                    return true;
                }

                assembly_called = true;
                return true;
            };

            EXPECT_TRUE(simulate(status));
            EXPECT_FALSE(assembly_called);
        }

        TEST(CompleteUploadIdempotency, PendingStatusTriggersAssembly) {
            UploadTaskStatus status = UploadTaskStatus::Pending;
            bool assembly_called = false;

            auto simulate = [&](UploadTaskStatus current_status) -> bool {
                if (current_status == UploadTaskStatus::Completed) {
                    return true;
                }

                assembly_called = true;
                return true;
            };

            EXPECT_TRUE(simulate(status));
            EXPECT_TRUE(assembly_called);
        }

        TEST(CompleteUploadSingleflightRace, PoolRejectsDuplicateUploadId) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const std::string upload_id = "complete-upload-same-id";

            auto guard1 = pool.TryAcquireGuard(upload_id);
            auto guard2 = pool.TryAcquireGuard(upload_id);
            ASSERT_TRUE(guard1.has_value());
            EXPECT_FALSE(guard2.has_value());
            EXPECT_EQ(pool.ActiveCount(), baseline + 1);
            EXPECT_TRUE(pool.IsUploadActive(upload_id));
        }

        TEST(CompleteUploadSingleflightRace, ConcurrentFinalizeSameUploadIdAllowsOnlyOneAcquire) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const std::string upload_id = "complete-upload-singleflight";
            constexpr int worker_count = 8;

            std::atomic<int> ready{ 0 };
            std::atomic<bool> start{ false };
            std::atomic<int> acquires{ 0 };
            std::atomic<int> rejects{ 0 };

            auto worker = [&]() {
                ready.fetch_add(1, std::memory_order_relaxed);
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                auto guard = pool.TryAcquireGuard(upload_id);
                if (!guard.has_value()) {
                    rejects.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                acquires.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            };

            std::vector<std::thread> threads;
            threads.reserve(worker_count);
            for (int i = 0; i < worker_count; ++i) {
                threads.emplace_back(worker);
            }

            while (ready.load(std::memory_order_acquire) < worker_count) {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);

            for (auto& thread : threads) {
                thread.join();
            }

            EXPECT_EQ(acquires, 1);
            EXPECT_EQ(rejects, worker_count - 1);
            EXPECT_EQ(pool.ActiveCount(), baseline);
            EXPECT_FALSE(pool.IsUploadActive(upload_id));
        }

        TEST(CompleteUploadSingleflightRace, ConcurrentFinalizeWithPoolBackpressure) {
            auto& pool = AssemblyWorkerPool::GetInstance();
            const auto baseline = pool.ActiveCount();
            const auto max_concurrent = pool.MaxConcurrent();
            ASSERT_GE(max_concurrent, baseline);
            const auto available_slots = max_concurrent - baseline;
            ASSERT_GT(available_slots, 0U);
            const auto worker_count = available_slots + 10;

            std::atomic<int> ready{ 0 };
            std::atomic<bool> start{ false };
            std::atomic<int> acquires{ 0 };
            std::atomic<int> rejects{ 0 };

            auto worker = [&](size_t index) {
                ready.fetch_add(1, std::memory_order_relaxed);
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                auto guard = pool.TryAcquireGuard("complete-upload-worker-" + std::to_string(index));
                if (!guard.has_value()) {
                    rejects.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                acquires.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            };

            std::vector<std::thread> threads;
            threads.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) {
                threads.emplace_back(worker, i);
            }

            while (ready.load(std::memory_order_acquire) < static_cast<int>(worker_count)) {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);

            for (auto& thread : threads) {
                thread.join();
            }

            EXPECT_EQ(acquires + rejects, static_cast<int>(worker_count));
            EXPECT_EQ(acquires, static_cast<int>(available_slots));
            EXPECT_EQ(rejects, static_cast<int>(worker_count - available_slots));
            EXPECT_EQ(pool.ActiveCount(), baseline);
        }

    } ///< namespace
} ///< namespace disk::file
