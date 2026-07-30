/**
 * @file RuntimeConfig.cpp
 * @brief Runtime JSON configuration loading and environment overrides
 *
 * @copyright Copyright (c) 2026
 */

#include "RuntimeConfig.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <json/reader.h>

namespace disk::utils {
    namespace {

        [[nodiscard]]
        auto ReadEnvironment(const char* name) -> std::optional<std::string> {
            const auto* value = std::getenv(name);
            if (value == nullptr) {
                return std::nullopt;
            }
            if (*value == '\0') {
                throw std::runtime_error(std::string("Environment variable must not be empty: ") + name);
            }
            return std::string(value);
        }

        [[nodiscard]]
        auto ParseInteger(
            const std::string& value,
            const char* name,
            int64_t minimum,
            int64_t maximum
        ) -> Json::Int64 {
            int64_t parsed = 0;
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                parsed
            );
            if (error != std::errc{} || end != value.data() + value.size() ||
                parsed < minimum || parsed > maximum) {
                throw std::runtime_error(
                    std::string("Invalid integer environment variable: ") + name
                );
            }
            return static_cast<Json::Int64>(parsed);
        }

        [[nodiscard]]
        auto ParseBoolean(const std::string& value, const char* name) -> bool {
            if (value == "true" || value == "1") {
                return true;
            }
            if (value == "false" || value == "0") {
                return false;
            }
            throw std::runtime_error(
                std::string("Invalid boolean environment variable: ") + name
            );
        }

        auto FirstObject(Json::Value& root, const char* section) -> Json::Value& {
            auto& values = root[section];
            if (!values.isArray() || values.empty() || !values[0].isObject()) {
                throw std::runtime_error(
                    std::string("Configuration must contain an object at ") + section + "[0]"
                );
            }
            return values[0];
        }

        auto DiskConfig(Json::Value& root) -> Json::Value& {
            auto& custom_config = root["custom_config"];
            if (!custom_config.isObject()) {
                throw std::runtime_error("Configuration must contain custom_config object");
            }
            auto& disk = custom_config["disk"];
            if (!disk.isObject()) {
                throw std::runtime_error("Configuration must contain custom_config.disk object");
            }
            return disk;
        }

        template <typename Setter>
        auto ApplyString(const char* name, Setter&& setter) -> void {
            if (auto value = ReadEnvironment(name); value.has_value()) {
                setter(std::move(*value));
            }
        }

        template <typename Setter>
        auto ApplyInteger(
            const char* name,
            int64_t minimum,
            int64_t maximum,
            Setter&& setter
        ) -> void {
            if (auto value = ReadEnvironment(name); value.has_value()) {
                setter(ParseInteger(*value, name, minimum, maximum));
            }
        }

        template <typename Setter>
        auto ApplyBoolean(const char* name, Setter&& setter) -> void {
            if (auto value = ReadEnvironment(name); value.has_value()) {
                setter(ParseBoolean(*value, name));
            }
        }

        [[nodiscard]]
        auto LoadConfigFile(std::string_view path) -> Json::Value {
            std::ifstream input{ std::string(path) };
            if (!input.is_open()) {
                throw std::runtime_error("Cannot open runtime configuration file");
            }

            Json::Value config;
            Json::CharReaderBuilder builder;
            std::string errors;
            if (!Json::parseFromStream(builder, input, &config, &errors) || !config.isObject()) {
                throw std::runtime_error("Cannot parse runtime configuration file: " + errors);
            }
            return config;
        }

    } // namespace

    auto RuntimeConfig::LoadFromEnvironment() -> Json::Value {
        auto path = ReadEnvironment("DISK_CONFIG_FILE").value_or("config.json");
        auto config = LoadConfigFile(path);
        ValidateDatabaseRouting(config);
        ApplyEnvironmentOverrides(config);
        return config;
    }

    auto RuntimeConfig::ValidateDatabaseRouting(const Json::Value& config) -> void {
        constexpr auto ERROR_MESSAGE =
            "Database routing configuration must contain exactly one client object named default";

        if (!config.isObject()) {
            throw std::runtime_error(ERROR_MESSAGE);
        }

        const auto& clients = config["db_clients"];
        if (!clients.isArray() || clients.size() != 1 || !clients[0].isObject()) {
            throw std::runtime_error(ERROR_MESSAGE);
        }

        const auto& name = clients[0]["name"];
        if (!name.isString() || name.asString() != "default") {
            throw std::runtime_error(ERROR_MESSAGE);
        }
    }

    auto RuntimeConfig::ApplyEnvironmentOverrides(Json::Value& config) -> void {
        if (!config.isObject()) {
            throw std::runtime_error("Runtime configuration root must be an object");
        }

        ApplyString("DISK_LISTEN_ADDRESS", [&config](std::string value) {
            FirstObject(config, "listeners")["address"] = std::move(value);
        });
        ApplyInteger("DISK_LISTEN_PORT", 1, 65535, [&config](Json::Int64 value) {
            FirstObject(config, "listeners")["port"] = value;
        });

        ApplyString("DATABASE_HOST", [&config](std::string value) {
            FirstObject(config, "db_clients")["host"] = std::move(value);
        });
        ApplyInteger("DATABASE_PORT", 1, 65535, [&config](Json::Int64 value) {
            FirstObject(config, "db_clients")["port"] = value;
        });
        ApplyString("DATABASE_NAME", [&config](std::string value) {
            FirstObject(config, "db_clients")["dbname"] = std::move(value);
        });
        ApplyString("DATABASE_USER", [&config](std::string value) {
            FirstObject(config, "db_clients")["user"] = std::move(value);
        });
        ApplyString("DATABASE_PASSWORD", [&config](std::string value) {
            FirstObject(config, "db_clients")["passwd"] = std::move(value);
        });
        ApplyInteger("DATABASE_POOL_SIZE", 1, 1024, [&config](Json::Int64 value) {
            FirstObject(config, "db_clients")["connection_number"] = value;
        });

        ApplyString("REDIS_HOST", [&config](std::string value) {
            FirstObject(config, "redis_clients")["host"] = std::move(value);
        });
        ApplyInteger("REDIS_PORT", 1, 65535, [&config](Json::Int64 value) {
            FirstObject(config, "redis_clients")["port"] = value;
        });
        ApplyInteger("REDIS_DB", 0, 255, [&config](Json::Int64 value) {
            FirstObject(config, "redis_clients")["db"] = value;
        });
        ApplyString("REDIS_PASSWORD", [&config](std::string value) {
            FirstObject(config, "redis_clients")["passwd"] = std::move(value);
        });
        ApplyInteger("REDIS_POOL_SIZE", 1, 1024, [&config](Json::Int64 value) {
            FirstObject(config, "redis_clients")["number_of_connections"] = value;
        });

        ApplyString("DISK_PROCESS_ROLE", [&config](std::string value) {
            DiskConfig(config)["process_role"] = std::move(value);
        });
        ApplyString("DISK_INSTANCE_ID", [&config](std::string value) {
            DiskConfig(config)["instance_id"] = std::move(value);
        });
        ApplyBoolean("DISK_WORKER_CLAIMING_ENABLED", [&config](bool value) {
            DiskConfig(config)["worker_claiming_enabled"] = value;
        });
        ApplyString("DISK_STORAGE_BACKEND", [&config](std::string value) {
            DiskConfig(config)["storage_backend"] = std::move(value);
        });
        ApplyString("DISK_UPLOAD_STAGING_BACKEND", [&config](std::string value) {
            DiskConfig(config)["upload_staging_backend"] = std::move(value);
        });
        ApplyBoolean("DISK_UPLOAD_TASK_CREATION_ENABLED", [&config](bool value) {
            DiskConfig(config)["upload_task_creation_enabled"] = value;
        });

        ApplyString("DISK_S3_BUCKET", [&config](std::string value) {
            DiskConfig(config)["s3"]["bucket"] = std::move(value);
        });
        ApplyString("DISK_S3_REGION", [&config](std::string value) {
            DiskConfig(config)["s3"]["region"] = std::move(value);
        });
        ApplyString("DISK_S3_ENDPOINT", [&config](std::string value) {
            DiskConfig(config)["s3"]["endpoint"] = std::move(value);
        });
        ApplyBoolean("DISK_S3_USE_SSL", [&config](bool value) {
            DiskConfig(config)["s3"]["use_ssl"] = value;
        });
        ApplyBoolean("DISK_S3_FORCE_PATH_STYLE", [&config](bool value) {
            DiskConfig(config)["s3"]["force_path_style"] = value;
        });
        ApplyBoolean("DISK_S3_VERIFY_SSL", [&config](bool value) {
            DiskConfig(config)["s3"]["verify_ssl"] = value;
        });
        ApplyString("DISK_S3_OBJECT_PREFIX", [&config](std::string value) {
            DiskConfig(config)["s3"]["object_prefix"] = std::move(value);
        });
        ApplyString("DISK_S3_STAGING_PREFIX", [&config](std::string value) {
            DiskConfig(config)["s3"]["staging_prefix"] = std::move(value);
        });
        ApplyInteger("DISK_S3_MAX_CONNECTIONS", 1, 256, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["max_connections"] = value;
        });
        ApplyInteger("DISK_S3_IO_THREADS", 1, 64, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["io_threads"] = value;
        });
        ApplyInteger("DISK_S3_CONNECT_TIMEOUT_MS", 100, 60000, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["connect_timeout_ms"] = value;
        });
        ApplyInteger("DISK_S3_REQUEST_TIMEOUT_MS", 1000, 3600000, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["request_timeout_ms"] = value;
        });
        ApplyInteger("DISK_S3_MAX_RETRIES", 0, 10, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["max_retries"] = value;
        });
        ApplyInteger("DISK_S3_RETRY_BASE_DELAY_MS", 1, 60000, [&config](Json::Int64 value) {
            DiskConfig(config)["s3"]["retry_base_delay_ms"] = value;
        });
    }

} // namespace disk::utils
