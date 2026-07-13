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
 * 5. ValidateSecureConfig gates DATABASE_PASSWORD/REDIS_PASSWORD on DISK_SECURE_MODE
 */

#include "utils/ConfigMgr.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include <drogon/drogon.h>
#include <gtest/gtest.h>

namespace {

    using disk::utils::ConfigMgr;

    /// ================================================================================
    /// EnvVarGuard — RAII helper for save/set/restore environment variables
    /// ================================================================================

    class EnvVarGuard {
    public:
        explicit EnvVarGuard(const char* name)
            : m_name(name), m_saved_value(SaveCurrent(name)) {}

        ~EnvVarGuard() { Restore(); }

        /// Disallow copy
        EnvVarGuard(const EnvVarGuard&) = delete;
        EnvVarGuard& operator=(const EnvVarGuard&) = delete;

        void Set(const char* value) {
            /// Unset first to avoid leaks from setenv replacing
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

    /// ================================================================================
    /// Fixture — guards all relevant env vars for each test
    /// ================================================================================

    class ConfigMgrJwtTest : public ::testing::Test {
    protected:
        void SetUp() override {
            /// Unset JWT_SECRET by default so tests start clean
            jwt_guard.Unset();
            secure_guard.Unset();
            mysql_guard.Unset();
            redis_guard.Unset();
        }

        EnvVarGuard jwt_guard{ "JWT_SECRET" };
        EnvVarGuard secure_guard{ "DISK_SECURE_MODE" };
        EnvVarGuard mysql_guard{ "DATABASE_PASSWORD" };
        EnvVarGuard redis_guard{ "REDIS_PASSWORD" };
    };

    /// ================================================================================
    /// GetJwtSecret — throws when JWT_SECRET is unset
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenEnvNotSet) {
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    /// ================================================================================
    /// GetJwtSecret — throws when JWT_SECRET is empty string
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenEmpty) {
        jwt_guard.Set("");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    /// ================================================================================
    /// GetJwtSecret — throws when JWT_SECRET < 32 chars
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenTooShort) {
        jwt_guard.Set("short_key_only_20_chars");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    /// ================================================================================
    /// GetJwtSecret — throws when JWT_SECRET is exactly 31 chars
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretThrowsWhenExactly31Chars) {
        jwt_guard.Set("1234567890123456789012345678901");
        EXPECT_THROW(
            { (void)ConfigMgr::GetInstance()->GetJwtSecret(); },
            std::runtime_error
        );
    }

    /// ================================================================================
    /// GetJwtSecret — returns value when exactly 32 chars
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretReturnsWhenExactly32Chars) {
        const char* valid_32 = "12345678901234567890123456789012";
        jwt_guard.Set(valid_32);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetJwtSecret(), valid_32);
    }

    /// ================================================================================
    /// GetJwtSecret — returns value when valid (> 32 chars)
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetJwtSecretReturnsWhenValidLongSecret) {
        const char* long_secret = "this_is_a_very_long_jwt_secret_key_that_is_definitely_over_32_chars";
        jwt_guard.Set(long_secret);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetJwtSecret(), long_secret);
    }

    /// ================================================================================
    /// ValidateSecureConfig — always checks JWT_SECRET, even without DISK_SECURE_MODE
    /// ================================================================================

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

    /// ================================================================================
    /// ValidateSecureConfig — passes with valid JWT_SECRET but no DISK_SECURE_MODE
    /// (MYSQL_PASSWORD/REDIS_PASSWORD not required in dev mode)
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigPassesWithValidJwtSecretNoSecureMode) {
        secure_guard.Unset();
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    /// ================================================================================
    /// ValidateSecureConfig — in secure mode, missing DATABASE_PASSWORD throws
    /// ================================================================================

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

    /// ================================================================================
    /// ValidateSecureConfig — in secure mode, missing REDIS_PASSWORD throws
    /// ================================================================================

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

    /// ================================================================================
    /// ValidateSecureConfig — in secure mode with all vars set, passes
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureConfigPassesWithAllVarsInSecureMode) {
        secure_guard.Set("true");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        mysql_guard.Set("mysql_pw");
        redis_guard.Set("redis_pw");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    /// ================================================================================
    /// ValidateSecureConfig — DISK_SECURE_MODE=1 also enables secure mode
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, ValidateSecureModeWithNumericOne) {
        secure_guard.Set("1");
        jwt_guard.Set("a_valid_jwt_secret_that_is_at_least_32_chars_long");
        mysql_guard.Set("mysql_pw");
        redis_guard.Set("redis_pw");
        EXPECT_NO_THROW({ ConfigMgr::GetInstance()->ValidateSecureConfig(); });
    }

    /// ================================================================================
    /// ValidateSecureConfig — DISK_SECURE_MODE=1, missing DATABASE_PASSWORD throws
    /// ================================================================================

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

    /// ================================================================================
    /// ValidateSecureConfig — short JWT_SECRET throws even in secure mode with DB passwords
    /// ================================================================================

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

    /// ================================================================================
    /// GetAssemblyMaxConcurrent — returns default value when not loaded
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetAssemblyMaxConcurrentReturnsDefault) {
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 4);
    }

    /// ================================================================================
    /// GetAssembleBufferSizeBytes — returns default value when not loaded
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, GetAssembleBufferSizeBytesReturnsDefault) {
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 1048576);
    }

    /// ================================================================================
    /// Assembly config — getters are accessible and return uint32_t
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, AssemblyGettersAreAccessible) {
        auto assembly_max_concurrent = ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent();
        auto assemble_buffer_size_bytes = ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes();

        EXPECT_GT(assembly_max_concurrent, 0);
        EXPECT_GT(assemble_buffer_size_bytes, 0);
    }

    /// ================================================================================
    /// Helper — restore assembly config to defaults after each LoadConfig test
    /// ================================================================================

    static auto RestoreAssemblyDefaults() -> void {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 4;
        cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = 1048576;
        drogon::app().loadConfigJson(cfg);
        ConfigMgr::GetInstance()->LoadConfig();
    }

    /// ================================================================================
    /// LoadConfig — explicit assembly values are read correctly
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, LoadConfigWithExplicitAssemblyValues) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 16;
        cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = 524288;
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 16);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 524288);

        RestoreAssemblyDefaults();
    }

    /// ================================================================================
    /// LoadConfig — missing assembly keys fall back to defaults
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, LoadConfigWithoutAssemblyKeysFallsBackToDefaults) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["storage_base_path"] = "test";
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 4);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 1048576);

        RestoreAssemblyDefaults();
    }

    /// ================================================================================
    /// LoadConfig — only assembly_max_concurrent present, buffer falls back
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, LoadConfigOnlyMaxConcurrentBufferFallsBack) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 8;
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 8);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 1048576);

        RestoreAssemblyDefaults();
    }

    /// ================================================================================
    /// LoadConfig — only assemble_buffer_size_bytes present, concurrent falls back
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, LoadConfigOnlyBufferSizeConcurrentFallsBack) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = 131072;
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 4);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 131072);

        RestoreAssemblyDefaults();
    }

    /// ================================================================================
    /// LoadConfig — no disk section at all, all values stay at defaults
    /// ================================================================================

    TEST_F(ConfigMgrJwtTest, LoadConfigNoDiskSectionStaysDefaults) {
        Json::Value cfg;
        cfg["custom_config"]["other"]["key"] = "value";
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent(), 4);
        EXPECT_EQ(ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes(), 1048576);

        RestoreAssemblyDefaults();
    }

    TEST_F(ConfigMgrJwtTest, StorageBackendDefaultsToLocal) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["storage_base_path"] = "test";
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetStorageBackend(), disk::utils::StorageBackend::Local);
        RestoreAssemblyDefaults();
    }

    TEST_F(ConfigMgrJwtTest, LoadConfigWithExplicitS3Backend) {
        Json::Value cfg;
        auto& disk = cfg["custom_config"]["disk"];
        disk["storage_backend"] = "s3";
        disk["s3"]["bucket"] = "disk-test";
        disk["s3"]["region"] = "us-west-2";
        disk["s3"]["endpoint"] = "http://127.0.0.1:9000";
        disk["s3"]["use_ssl"] = false;
        disk["s3"]["force_path_style"] = true;
        disk["s3"]["verify_ssl"] = false;
        disk["s3"]["object_prefix"] = "/uploads/objects/";
        disk["s3"]["connect_timeout_ms"] = 1234;
        disk["s3"]["request_timeout_ms"] = 5678;
        drogon::app().loadConfigJson(cfg);

        ConfigMgr::GetInstance()->LoadConfig();

        EXPECT_EQ(ConfigMgr::GetInstance()->GetStorageBackend(), disk::utils::StorageBackend::S3);
        const auto s3 = ConfigMgr::GetInstance()->GetS3StorageConfig();
        EXPECT_EQ(s3.bucket, "disk-test");
        EXPECT_EQ(s3.region, "us-west-2");
        EXPECT_EQ(s3.endpoint, "http://127.0.0.1:9000");
        EXPECT_FALSE(s3.use_ssl);
        EXPECT_TRUE(s3.force_path_style);
        EXPECT_FALSE(s3.verify_ssl);
        EXPECT_EQ(s3.object_prefix, "uploads/objects");
        EXPECT_EQ(s3.connect_timeout_ms, 1234);
        EXPECT_EQ(s3.request_timeout_ms, 5678);

        RestoreAssemblyDefaults();
    }

    TEST_F(ConfigMgrJwtTest, LoadConfigRejectsInvalidStorageBackend) {
        Json::Value cfg;
        cfg["custom_config"]["disk"]["storage_backend"] = "ftp";
        drogon::app().loadConfigJson(cfg);

        EXPECT_THROW({ ConfigMgr::GetInstance()->LoadConfig(); }, std::runtime_error);
        RestoreAssemblyDefaults();
    }

    TEST_F(ConfigMgrJwtTest, LoadConfigRejectsS3BackendWithoutBucket) {
        Json::Value cfg;
        auto& disk = cfg["custom_config"]["disk"];
        disk["storage_backend"] = "s3";
        disk["s3"]["bucket"] = "";
        disk["s3"]["region"] = "us-east-1";
        drogon::app().loadConfigJson(cfg);

        EXPECT_THROW({ ConfigMgr::GetInstance()->LoadConfig(); }, std::runtime_error);
        RestoreAssemblyDefaults();
    }

} ///< namespace
