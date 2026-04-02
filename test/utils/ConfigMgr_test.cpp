/**
 * @file ConfigMgr_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ConfigMgr JWT secret enforcement tests
 *
 * @copyright Copyright (c) 2026
 *
 * Tests the new JWT_SECRET enforcement behavior:
 * 1. GetJwtSecret throws when JWT_SECRET unset
 * 2. GetJwtSecret throws when JWT_SECRET < 32 chars
 * 3. GetJwtSecret returns value when valid
 * 4. ValidateSecureConfig always checks JWT_SECRET regardless of DISK_SECURE_MODE
 * 5. ValidateSecureConfig gates MYSQL_PASSWORD/REDIS_PASSWORD on DISK_SECURE_MODE
 */

#include "utils/ConfigMgr.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

namespace {

    using disk::utils::ConfigMgr;

    // ================================================================================
    // EnvVarGuard — RAII helper for save/set/restore environment variables
    // ================================================================================

    class EnvVarGuard {
    public:
        explicit EnvVarGuard(const char* name)
            : m_name(name), m_saved_value(SaveCurrent(name)) {}

        ~EnvVarGuard() { Restore(); }

        // Disallow copy
        EnvVarGuard(const EnvVarGuard&) = delete;
        EnvVarGuard& operator=(const EnvVarGuard&) = delete;

        void Set(const char* value) {
            // Unset first to avoid leaks from setenv replacing
            unsetenv(m_name.c_str());
            if (value != nullptr) {
                setenv(m_name.c_str(), value, 1);
            }
        }

        void Unset() { unsetenv(m_name.c_str()); }

    private:
        static auto SaveCurrent(const std::string& name) -> char* {
            const char* current = std::getenv(name.c_str());
            return (current != nullptr) ? strdup(current) : nullptr;
        }

        void Restore() {
            unsetenv(m_name.c_str());
            if (m_saved_value != nullptr) {
                setenv(m_name.c_str(), m_saved_value, 1);
                free(m_saved_value);
                m_saved_value = nullptr;
            }
        }

        std::string m_name;
        char* m_saved_value;
    };

    // ================================================================================
    // Fixture — guards all relevant env vars for each test
    // ================================================================================

    class ConfigMgrJwtTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // Unset JWT_SECRET by default so tests start clean
            jwt_guard.Unset();
            secure_guard.Unset();
            mysql_guard.Unset();
            redis_guard.Unset();
        }

        EnvVarGuard jwt_guard{ "JWT_SECRET" };
        EnvVarGuard secure_guard{ "DISK_SECURE_MODE" };
        EnvVarGuard mysql_guard{ "MYSQL_PASSWORD" };
        EnvVarGuard redis_guard{ "REDIS_PASSWORD" };
    };

    // ================================================================================
    // GetJwtSecret — throws when JWT_SECRET is unset
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenEnvNotSet) {
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // GetJwtSecret — throws when JWT_SECRET is empty string
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenEmpty) {
        jwt_guard.Set("");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // GetJwtSecret — throws when JWT_SECRET < 32 chars
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenTooShort) {
        jwt_guard.Set("short_key_only_20_chars");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // GetJwtSecret — throws when JWT_SECRET is exactly 31 chars
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenExactly31Chars) {
        jwt_guard.Set("1234567890123456789012345678901");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // GetJwtSecret — returns value when exactly 32 chars
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretReturnsWhenExactly32Chars) {
        const char* valid_32 = "12345678901234567890123456789012";
        jwt_guard.Set(valid_32);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetJwtSecret(), valid_32);
    }

    // ================================================================================
    // GetJwtSecret — returns value when valid (> 32 chars)
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretReturnsWhenValidLongSecret) {
        const char* long_secret = "this_is_a_very_long_jwt_secret_key_that_is_definitely_over_32_chars";
        jwt_guard.Set(long_secret);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetJwtSecret(), long_secret);
    }

    // ================================================================================
    // ValidateSecureConfig — always checks JWT_SECRET, even without DISK_SECURE_MODE
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigThrowsOnMissingJwtSecretWithoutSecureMode) {
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigThrowsOnShortJwtSecretWithoutSecureMode) {
        secure_guard.Unset();
        jwt_guard.Set("too_short");
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // ValidateSecureConfig — passes with valid JWT_SECRET but no DISK_SECURE_MODE
    // (MYSQL_PASSWORD/REDIS_PASSWORD not required in dev mode)
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigPassesWithValidJwtSecretNoSecureMode) {
        secure_guard.Unset();
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    // ================================================================================
    // ValidateSecureConfig — in secure mode, missing MYSQL_PASSWORD throws
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigThrowsOnMissingMysqlPasswordInSecureMode) {
        secure_guard.Set("true");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        redis_guard.Set("some_redis_password");
        mysql_guard.Unset();
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // ValidateSecureConfig — in secure mode, missing REDIS_PASSWORD throws
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigThrowsOnMissingRedisPasswordInSecureMode) {
        secure_guard.Set("true");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        mysql_guard.Set("some_mysql_password");
        redis_guard.Unset();
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // ValidateSecureConfig — in secure mode with all vars set, passes
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigPassesWithAllVarsInSecureMode) {
        secure_guard.Set("true");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        mysql_guard.Set("mysql_pw");
        redis_guard.Set("redis_pw");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    // ================================================================================
    // ValidateSecureConfig — DISK_SECURE_MODE=1 also enables secure mode
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureModeWithNumericOne) {
        secure_guard.Set("1");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        mysql_guard.Set("mysql_pw");
        redis_guard.Set("redis_pw");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    // ================================================================================
    // ValidateSecureConfig — DISK_SECURE_MODE=1, missing MYSQL_PASSWORD throws
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureModeWithOneMissingMysqlThrows) {
        secure_guard.Set("1");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        redis_guard.Set("redis_pw");
        mysql_guard.Unset();
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

    // ================================================================================
    // ValidateSecureConfig — short JWT_SECRET throws even in secure mode with DB passwords
    // ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigShortJwtSecretThrowsEvenWithDbPasswords) {
        secure_guard.Set("true");
        jwt_guard.Set("too_short");
        mysql_guard.Set("mysql_pw");
        redis_guard.Set("redis_pw");
        EXPECT_THROW(
            { ConfigMgr::GetInstance()->ValidateSecureConfig(); },
            std::runtime_error
        );
    }

} // namespace
