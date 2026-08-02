#include "utils/RuntimeConfig.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/writer.h>

namespace {

    template <typename Config>
    concept HasPublicLoadFile = requires(std::string_view path) {
        Config::LoadFile(path);
    };

    template <typename Config>
    concept HasPublicDatabaseRoutingValidation = requires(const Json::Value& config) {
        Config::ValidateDatabaseRouting(config);
    };

    template <typename Config>
    concept HasPublicEnvironmentOverrides = requires(Json::Value& config) {
        Config::ApplyEnvironmentOverrides(config);
    };

    static_assert(!HasPublicLoadFile<disk::utils::RuntimeConfig>);
    static_assert(!HasPublicDatabaseRoutingValidation<disk::utils::RuntimeConfig>);
    static_assert(!HasPublicEnvironmentOverrides<disk::utils::RuntimeConfig>);

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

    class RuntimeConfigFile final {
    public:
        explicit RuntimeConfigFile(const Json::Value& config)
            : m_path(
                  std::filesystem::temp_directory_path() /
                  ("disk-runtime-config-" + drogon::utils::getUuid() + ".json")
              ) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";

            std::ofstream output{ m_path };
            output << Json::writeString(builder, config);
            if (!output) {
                throw std::runtime_error("Cannot write temporary runtime configuration");
            }
        }

        ~RuntimeConfigFile() {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        RuntimeConfigFile(const RuntimeConfigFile&) = delete;
        RuntimeConfigFile& operator=(const RuntimeConfigFile&) = delete;

        [[nodiscard]]
        auto Path() const -> const std::filesystem::path& {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

    auto RuntimeEnvironmentNames() -> std::vector<std::string> {
        return {
            "DISK_CONFIG_FILE",
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
            "DISK_WORKER_CLAIMING_ENABLED",
            "DISK_STORAGE_BACKEND",
            "DISK_UPLOAD_STAGING_BACKEND",
            "DISK_UPLOAD_TASK_CREATION_ENABLED",
            "DISK_TRUSTED_PROXY_CIDRS",
            "DISK_S3_BUCKET",
            "DISK_S3_REGION",
            "DISK_S3_ENDPOINT",
            "DISK_S3_USE_SSL",
            "DISK_S3_FORCE_PATH_STYLE",
            "DISK_S3_VERIFY_SSL",
            "DISK_S3_OBJECT_PREFIX",
            "DISK_S3_STAGING_PREFIX",
            "DISK_S3_MAX_CONNECTIONS",
            "DISK_S3_IO_THREADS",
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
        config["db_clients"][0]["name"] = "default";
        config["db_clients"][0]["port"] = 5432;
        config["redis_clients"][0]["host"] = "localhost";
        config["redis_clients"][0]["port"] = 6379;
        config["custom_config"]["disk"] = Json::Value(Json::objectValue);
        config["plugins"][0]["name"] = "drogon::plugin::RealIpResolver";
        config["plugins"][0]["config"]["trust_ips"] = Json::Value(Json::arrayValue);
        return config;
    }

    auto LoadRuntimeConfig(const Json::Value& config) -> Json::Value {
        RuntimeConfigFile config_file(config);
        const auto path = config_file.Path().string();
        EnvironmentScope::Set("DISK_CONFIG_FILE", path.c_str());
        return disk::utils::RuntimeConfig::LoadFromEnvironment();
    }

    TEST(RuntimeConfigTest, AcceptsSingleDefaultDatabaseClient) {
        EnvironmentScope environment(RuntimeEnvironmentNames());

        EXPECT_NO_THROW(static_cast<void>(LoadRuntimeConfig(BaseConfig())));
    }

    TEST(RuntimeConfigTest, RejectsMalformedOrAmbiguousDatabaseRouting) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        Json::Value missing_clients(Json::objectValue);
        EXPECT_THROW(
            LoadRuntimeConfig(missing_clients),
            std::runtime_error
        );

        Json::Value object_clients(Json::objectValue);
        object_clients["db_clients"] = Json::Value(Json::objectValue);
        EXPECT_THROW(
            LoadRuntimeConfig(object_clients),
            std::runtime_error
        );

        Json::Value empty_clients(Json::objectValue);
        empty_clients["db_clients"] = Json::Value(Json::arrayValue);
        EXPECT_THROW(
            LoadRuntimeConfig(empty_clients),
            std::runtime_error
        );

        Json::Value scalar_client(Json::objectValue);
        scalar_client["db_clients"][0] = "default";
        EXPECT_THROW(
            LoadRuntimeConfig(scalar_client),
            std::runtime_error
        );

        auto renamed_client = BaseConfig();
        renamed_client["db_clients"][0]["name"] = "replica";
        EXPECT_THROW(
            LoadRuntimeConfig(renamed_client),
            std::runtime_error
        );

        auto multiple_clients = BaseConfig();
        multiple_clients["db_clients"][1]["name"] = "replica";
        EXPECT_THROW(
            LoadRuntimeConfig(multiple_clients),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, LoadRejectsReplicaWithoutEchoingConfigurationValues) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        auto config = BaseConfig();
        config["db_clients"][1]["name"] = "replica-secret-name";
        config["db_clients"][1]["host"] = "replica-secret-host";
        config["db_clients"][1]["passwd"] = "replica-secret-password";

        try {
            const auto loaded = LoadRuntimeConfig(config);
            static_cast<void>(loaded);
            FAIL() << "Expected additional database client to fail";
        } catch (const std::runtime_error& error) {
            const std::string message(error.what());
            EXPECT_NE(message.find("exactly one"), std::string::npos);
            EXPECT_EQ(message.find("replica-secret-name"), std::string::npos);
            EXPECT_EQ(message.find("replica-secret-host"), std::string::npos);
            EXPECT_EQ(message.find("replica-secret-password"), std::string::npos);
        }
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
        EnvironmentScope::Set("DISK_WORKER_CLAIMING_ENABLED", "false");
        EnvironmentScope::Set("DISK_STORAGE_BACKEND", "s3");
        EnvironmentScope::Set("DISK_UPLOAD_STAGING_BACKEND", "s3");
        EnvironmentScope::Set("DISK_UPLOAD_TASK_CREATION_ENABLED", "false");
        EnvironmentScope::Set(
            "DISK_TRUSTED_PROXY_CIDRS",
            R"(["10.20.0.10","10.20.1.0/24","128.0.0.0/1","203.0.113.41/32"])"
        );
        EnvironmentScope::Set("DISK_S3_BUCKET", "disk-test");
        EnvironmentScope::Set("DISK_S3_REGION", "us-west-2");
        EnvironmentScope::Set("DISK_S3_ENDPOINT", "https://minio:9000");
        EnvironmentScope::Set("DISK_S3_USE_SSL", "1");
        EnvironmentScope::Set("DISK_S3_FORCE_PATH_STYLE", "true");
        EnvironmentScope::Set("DISK_S3_VERIFY_SSL", "0");
        EnvironmentScope::Set("DISK_S3_OBJECT_PREFIX", "final");
        EnvironmentScope::Set("DISK_S3_STAGING_PREFIX", "incoming");
        EnvironmentScope::Set("DISK_S3_MAX_CONNECTIONS", "32");
        EnvironmentScope::Set("DISK_S3_IO_THREADS", "8");
        EnvironmentScope::Set("DISK_S3_CONNECT_TIMEOUT_MS", "1234");
        EnvironmentScope::Set("DISK_S3_REQUEST_TIMEOUT_MS", "5678");
        EnvironmentScope::Set("DISK_S3_MAX_RETRIES", "5");
        EnvironmentScope::Set("DISK_S3_RETRY_BASE_DELAY_MS", "250");

        auto config = LoadRuntimeConfig(BaseConfig());

        EXPECT_EQ(config["listeners"][0]["address"].asString(), "0.0.0.0");
        EXPECT_EQ(config["listeners"][0]["port"].asInt(), 18080);
        EXPECT_EQ(config["db_clients"][0]["host"].asString(), "postgres");
        EXPECT_EQ(config["db_clients"][0]["port"].asInt(), 5544);
        EXPECT_EQ(config["db_clients"][0]["dbname"].asString(), "disk_cluster");
        EXPECT_EQ(config["db_clients"][0]["user"].asString(), "disk_app");
        EXPECT_EQ(config["db_clients"][0]["passwd"].asString(), "db-test-password");
        EXPECT_EQ(config["db_clients"][0]["connection_number"].asInt(), 16);
        EXPECT_FALSE(config["db_clients"][0].isMember("num_connection_number"));
        EXPECT_EQ(config["redis_clients"][0]["host"].asString(), "redis");
        EXPECT_EQ(config["redis_clients"][0]["port"].asInt(), 6380);
        EXPECT_EQ(config["redis_clients"][0]["db"].asInt(), 3);
        EXPECT_EQ(config["redis_clients"][0]["passwd"].asString(), "redis-test-password");
        EXPECT_EQ(config["redis_clients"][0]["number_of_connections"].asInt(), 8);

        const auto& disk = config["custom_config"]["disk"];
        EXPECT_EQ(disk["process_role"].asString(), "worker");
        EXPECT_EQ(disk["instance_id"].asString(), "disk-worker-a");
        EXPECT_FALSE(disk["worker_claiming_enabled"].asBool());
        EXPECT_EQ(disk["storage_backend"].asString(), "s3");
        EXPECT_EQ(disk["upload_staging_backend"].asString(), "s3");
        EXPECT_FALSE(disk["upload_task_creation_enabled"].asBool());
        const auto& trust_ips = config["plugins"][0]["config"]["trust_ips"];
        ASSERT_TRUE(trust_ips.isArray());
        ASSERT_EQ(trust_ips.size(), 4U);
        EXPECT_EQ(trust_ips[0].asString(), "10.20.0.10");
        EXPECT_EQ(trust_ips[1].asString(), "10.20.1.0/24");
        EXPECT_EQ(trust_ips[2].asString(), "128.0.0.0/1");
        EXPECT_EQ(trust_ips[3].asString(), "203.0.113.41/32");
        EXPECT_EQ(disk["s3"]["bucket"].asString(), "disk-test");
        EXPECT_EQ(disk["s3"]["region"].asString(), "us-west-2");
        EXPECT_EQ(disk["s3"]["endpoint"].asString(), "https://minio:9000");
        EXPECT_TRUE(disk["s3"]["use_ssl"].asBool());
        EXPECT_TRUE(disk["s3"]["force_path_style"].asBool());
        EXPECT_FALSE(disk["s3"]["verify_ssl"].asBool());
        EXPECT_EQ(disk["s3"]["object_prefix"].asString(), "final");
        EXPECT_EQ(disk["s3"]["staging_prefix"].asString(), "incoming");
        EXPECT_EQ(disk["s3"]["max_connections"].asInt(), 32);
        EXPECT_EQ(disk["s3"]["io_threads"].asInt(), 8);
        EXPECT_EQ(disk["s3"]["connect_timeout_ms"].asInt(), 1234);
        EXPECT_EQ(disk["s3"]["request_timeout_ms"].asInt(), 5678);
        EXPECT_EQ(disk["s3"]["max_retries"].asInt(), 5);
        EXPECT_EQ(disk["s3"]["retry_base_delay_ms"].asInt(), 250);
    }

    TEST(RuntimeConfigTest, RejectsInvalidIntegerWithoutEchoingValue) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DATABASE_PORT", "not-a-port-secret");

        try {
            static_cast<void>(LoadRuntimeConfig(BaseConfig()));
            FAIL() << "Expected invalid port to fail";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("DATABASE_PORT"), std::string::npos);
            EXPECT_EQ(std::string(error.what()).find("not-a-port-secret"), std::string::npos);
        }
    }

    TEST(RuntimeConfigTest, RejectsOutOfRangeS3RetryBudget) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DISK_S3_MAX_RETRIES", "11");

        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RejectsOutOfRangeS3Capacity) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        EnvironmentScope::Set("DISK_S3_MAX_CONNECTIONS", "257");

        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RejectsInvalidBooleanAndEmptyOverride) {
        EnvironmentScope environment(RuntimeEnvironmentNames());

        EnvironmentScope::Set("DISK_S3_USE_SSL", "yes");
        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );

        EnvironmentScope::Set("DISK_S3_USE_SSL", "true");
        EnvironmentScope::Set("DISK_WORKER_CLAIMING_ENABLED", "disabled");
        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );

        EnvironmentScope::Set("DISK_WORKER_CLAIMING_ENABLED", "true");
        EnvironmentScope::Set("DISK_UPLOAD_TASK_CREATION_ENABLED", "disabled");
        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );

        EnvironmentScope::Set("DISK_UPLOAD_TASK_CREATION_ENABLED", "true");
        EnvironmentScope::Set("DATABASE_HOST", "");
        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RequiresTargetSectionOnlyWhenItsOverrideIsPresent) {
        EnvironmentScope environment(RuntimeEnvironmentNames());
        Json::Value config(Json::objectValue);
        config["db_clients"][0]["name"] = "default";
        EXPECT_NO_THROW(static_cast<void>(LoadRuntimeConfig(config)));

        EnvironmentScope::Set("REDIS_HOST", "redis");
        EXPECT_THROW(
            LoadRuntimeConfig(config),
            std::runtime_error
        );
    }

    TEST(RuntimeConfigTest, RejectsInvalidTrustedProxyCidrsWithoutEchoingValues) {
        EnvironmentScope environment(RuntimeEnvironmentNames());

        for (const auto* invalid : {
                 "not-json-secret",
                 "{}",
                 "[]",
                 R"([""])",
                 R"([42])",
                 R"(["not-an-ip-secret"])",
                 R"(["2001:db8::1"])",
                 R"(["10.20.0.1:8080"])",
                 R"([" 10.20.0.1"])",
                 R"(["010.20.0.1"])",
                 R"(["10.20.0"])",
                 R"(["10.20.0.256"])",
                 R"(["10.20.0.0/0"])",
                 R"(["10.20.0.0/"])",
                 R"(["10.20.0.0/+24"])",
                 R"(["10.20.0.0/33"])",
                 R"(["10.20.0.0/24/1"])",
                 R"(["10.20.0.1/24"])",
             }) {
            EnvironmentScope::Set("DISK_TRUSTED_PROXY_CIDRS", invalid);
            try {
                static_cast<void>(LoadRuntimeConfig(BaseConfig()));
                FAIL() << "Expected invalid trusted proxy CIDRs to fail";
            } catch (const std::runtime_error& error) {
                EXPECT_NE(
                    std::string(error.what()).find("DISK_TRUSTED_PROXY_CIDRS"),
                    std::string::npos
                );
                EXPECT_EQ(std::string(error.what()).find(invalid), std::string::npos);
            }
        }

        Json::Value too_many(Json::arrayValue);
        for (size_t index = 0; index < 33; ++index) {
            too_many.append("10.0.0.1");
        }
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        const auto too_many_json = Json::writeString(writer, too_many);
        EnvironmentScope::Set("DISK_TRUSTED_PROXY_CIDRS", too_many_json.c_str());
        EXPECT_THROW(
            LoadRuntimeConfig(BaseConfig()),
            std::runtime_error
        );

        EnvironmentScope::Set("DISK_TRUSTED_PROXY_CIDRS", R"(["10.20.0.10"])");
        auto missing_plugin = BaseConfig();
        missing_plugin.removeMember("plugins");
        EXPECT_THROW(
            LoadRuntimeConfig(missing_plugin),
            std::runtime_error
        );
    }

} // namespace
