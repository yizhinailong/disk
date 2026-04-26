/**
 * @file main.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 测试主入口
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <gtest/gtest.h>
#include <sodium.h>

class PasswdHashTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (sodium_init() < 0) {
            std::cerr << "Failed to initialize libsodium" << std::endl;
            exit(1);
        }
    }
};

auto main(int argc, char** argv) -> int {
    auto env = std::make_unique<PasswdHashTestEnvironment>();
    ::testing::AddGlobalTestEnvironment(env.release());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
