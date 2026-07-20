#include "utils/RuntimeConfig.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

    class EnvironmentScope final {
    public:
        explicit EnvironmentScope(std::vector<std::string> names) {
            m_values.reserve(names.size());
            for (auto& name : names) {
                const auto* current = std::getenv(name.c_str());
                m_values.emplace_back(
                    std::move(name),
                    current == nullptr ? nullptr : strdup(current)
                );
                unsetenv(m_values.back().first.c_str());
            }
        }

        ~EnvironmentScope() {
            for (auto& [name, value] : m_values) {
                unsetenv(name.c_str());
                if (value != nullptr) {
                    setenv(name.c_str(), value, 1);
                    free(value);
                }
            }
        }

        EnvironmentScope(const EnvironmentScope&) = delete;
        EnvironmentScope& operator=(const EnvironmentScope&) = delete;

        static auto Set(const char* name, const char* value) -> void {
            setenv(name, value, 1);
        }

    private:
        std::vector<std::pair<std::string, char*>> m_values;
    };

    auto RuntimeEnvironmentNames() -> std::vector<std::string> {
        return {
            "DISK_LISTEN_ADDRESS",
            "DISK_LISTEN_PORT",
            "DATABASE_HOST",
            "DATABASE_PORT",
            "DATABASE_NAME",
            "DATABASE_USER",
            "DATABASE_PASSWORD",
            "DATABASE_POOL_SIZE",
            "REDIS_HOST",
            "REDIS_PORT",
            "REDIS_DB",
            "REDIS_PASSWORD",
            "REDIS_POOL_SIZE",
            "DISK_PROCESS_ROLE",
            "DISK_INSTANCE_ID",
            "DISK_STORAGE_BACKEND",
            "DISK_UPLOAD_STAGING_BACKEND",
            "DISK_S3_BUCKET",
            "DISK_S3_REGION",
            "DISK_S3_ENDPOINT",
            "DISK_S3_USE_SSL",
            "DISK_S3_FORCE_PATH_STYLE",
            "DISK_S3_VERIFY_SSL",
            "DISK_S3_OBJECT_PREFIX",
            "DISK_S3_STAGING_PREFIX",
            "DISK_S3_CONNECT_TIMEOUT_MS",
            "DISK_S3_REQUEST_TIMEOUT_MS",
            "DISK_S3_MAX_RETRIES",
            "DISK_S3_RETRY_BASE_DELAY_MS",
        };
    }

    auto BaseConfig() -> Json::Value {
        Json::Value config;
        config["listeners"][0]["address"] = "127.0.0.1";
        config["listeners"][0]["port"] = 8080;
        config["db_clients"][0]["host"] = "localhost";
        config["db_clients"][0]["port"] = 5432;
        config["redis_clients"][0]["host"] = "localhost";
        config["redis_clients"][0]["port"] = 6379;
        config["custom_config"]["disk"] = Json::Value(Json::objectValue);
        return config;
    }

    TEST(RuntimeConfigTest, AppliesTypedEnvironmentOverrides) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DISK_LISTEN_ADDRESS", "0.0.0.0");
        EnvironmentScope::Set("DISK_LISTEN_PORT", "18080");
        EnvironmentScope::Set("DATABASE_HOST", "postgres");
        EnvironmentScope::Set("DATABASE_PORT", "5544");
        EnvironmentScope::Set("DATABASE_NAME", "disk_cluster");
        EnvironmentScope::Set("DATABASE_USER", "disk_app");
        EnvironmentScope::Set("DATABASE_PASSWORD", "db-test-password");
        EnvironmentScope::Set("DATABASE_POOL_SIZE", "16");
        EnvironmentScope::Set("REDIS_HOST", "redis");
        EnvironmentScope::Set("REDIS_PORT", "6380");
        EnvironmentScope::Set("REDIS_DB", "3");
        EnvironmentScope::Set("REDIS_PASSWORD", "redis-test-password");
        EnvironmentScope::Set("REDIS_POOL_SIZE", "8");
        EnvironmentScope::Set("DISK_PROCESS_ROLE", "worker");
        EnvironmentScope::Set("DISK_INSTANCE_ID", "disk-worker-a");
        EnvironmentScope::Set("DISK_STORAGE_BACKEND", "s3");
        EnvironmentScope::Set("DISK_UPLOAD_STAGING_BACKEND", "s3");
        EnvironmentScope::Set("DISK_S3_BUCKET", "disk-test");
        EnvironmentScope::Set("DISK_S3_REGION", "us-west-2");
        EnvironmentScope::Set("DISK_S3_ENDPOINT", "https://minio:9000");
        EnvironmentScope::Set("DISK_S3_USE_SSL", "1");
        EnvironmentScope::Set("DISK_S3_FORCE_PATH_STYLE", "true");
        EnvironmentScope::Set("DISK_S3_VERIFY_SSL", "0");
        EnvironmentScope::Set("DISK_S3_OBJECT_PREFIX", "final");
        EnvironmentScope::Set("DISK_S3_STAGING_PREFIX", "incoming");
        EnvironmentScope::Set("DISK_S3_CONNECT_TIMEOUT_MS", "1234");
        EnvironmentScope::Set("DISK_S3_REQUEST_TIMEOUT_MS", "5678");
        EnvironmentScope::Set("DISK_S3_MAX_RETRIES", "5");
        EnvironmentScope::Set("DISK_S3_RETRY_BASE_DELAY_MS", "250");

        auto config = BaseConfig();
        disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config);

        EXPECT_EQ(config["listeners"][0]["address"].asString(), "0.0.0.0");
        EXPECT_EQ(config["listeners"][0]["port"].asInt(), 18080);
        EXPECT_EQ(config["db_clients"][0]["host"].asString(), "postgres");
        EXPECT_EQ(config["db_clients"][0]["port"].asInt(), 5544);
        EXPECT_EQ(config["db_clients"][0]["dbname"].asString(), "disk_cluster");
        EXPECT_EQ(config["db_clients"][0]["user"].asString(), "disk_app");
        EXPECT_EQ(config["db_clients"][0]["passwd"].asString(), "db-test-password");
        EXPECT_EQ(config["db_clients"][0]["num_connection_number"].asInt(), 16);
        EXPECT_EQ(config["redis_clients"][0]["host"].asString(), "redis");
        EXPECT_EQ(config["redis_clients"][0]["port"].asInt(), 6380);
        EXPECT_EQ(config["redis_clients"][0]["db"].asInt(), 3);
        EXPECT_EQ(config["redis_clients"][0]["passwd"].asString(), "redis-test-password");
        EXPECT_EQ(config["redis_clients"][0]["number_of_connections"].asInt(), 8);

        const auto& disk = config["custom_config"]["disk"];
        EXPECT_EQ(disk["process_role"].asString(), "worker");
        EXPECT_EQ(disk["instance_id"].asString(), "disk-worker-a");
        EXPECT_EQ(disk["storage_backend"].asString(), "s3");
        EXPECT_EQ(disk["upload_staging_backend"].asString(), "s3");
        EXPECT_EQ(disk["s3"]["bucket"].asString(), "disk-test");
        EXPECT_EQ(disk["s3"]["region"].asString(), "us-west-2");
        EXPECT_EQ(disk["s3"]["endpoint"].asString(), "https://minio:9000");
        EXPECT_TRUE(disk["s3"]["use_ssl"].asBool());
        EXPECT_TRUE(disk["s3"]["force_path_style"].asBool());
        EXPECT_FALSE(disk["s3"]["verify_ssl"].asBool());
        EXPECT_EQ(disk["s3"]["object_prefix"].asString(), "final");
        EXPECT_EQ(disk["s3"]["staging_prefix"].asString(), "incoming");
        EXPECT_EQ(disk["s3"]["connect_timeout_ms"].asInt(), 1234);
        EXPECT_EQ(disk["s3"]["request_timeout_ms"].asInt(), 5678);
        EXPECT_EQ(disk["s3"]["max_retries"].asInt(), 5);
        EXPECT_EQ(disk["s3"]["retry_base_delay_ms"].asInt(), 250);
    }

    TEST(RuntimeConfigTest, RejectsInvalidIntegerWithoutEchoingValue) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DATABASE_PORT", "not-a-port-secret");
        auto config = BaseConfig();

        try {
            disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config);
            FAIL() << "Expected invalid port to fail";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("DATABASE_PORT"), std::string::npos);
            EXPECT_EQ(std::string(error.what()).find("not-a-port-secret"), std::string::npos);
        }
    }

    TEST(RuntimeConfigTest, RejectsOutOfRangeS3RetryBudget) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DISK_S3_MAX_RETRIES", "11");
        auto config = BaseConfig();

        EXPECT_THROW(
            disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RejectsInvalidBooleanAndEmptyOverride) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        auto config = BaseConfig();

        EnvironmentScope::Set("DISK_S3_USE_SSL", "yes");
        EXPECT_THROW(
            disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config),
            std::runtime_error
        );

        EnvironmentScope::Set("DISK_S3_USE_SSL", "true");
        EnvironmentScope::Set("DATABASE_HOST", "");
        EXPECT_THROW(
            disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RequiresTargetSectionOnlyWhenItsOverrideIsPresent) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        Json::Value config(Json::objectValue);
        EXPECT_NO_THROW(disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config));

        EnvironmentScope::Set("REDIS_HOST", "redis");
        EXPECT_THROW(
            disk::utils::RuntimeConfig::ApplyEnvironmentOverrides(config),
            std::runtime_error
        );
    }

} // namespace
