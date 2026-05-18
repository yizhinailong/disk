/**
 * @file Pch.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 预编译头文件（包含所有第三方库头文件）
 *
 * @details
 * 本文件包含项目中使用的所有第三方库头文件，按类别组织：
 * - C++ 标准库
 * - Drogon Web 框架（含 ORM、HTTP、Redis 客户端、网络层）
 * - JsonCpp JSON 解析库
 * - jwt-cpp JWT 认证库
 * - libsodium 加密库
 * - Trantor 网络库（Drogon 底层）
 * - Google Test 测试框架
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

// C++ 标准库常用头文件
#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// ==================== Drogon 框架头文件 ====================
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/BaseBuilder.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/Utilities.h>

// ==================== JSON 库头文件 ====================
#include <json/json.h>

// ==================== JWT 库头文件 ====================
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

// ==================== libsodium 加密库头文件 ====================
#include <sodium.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_pwhash.h>

// ==================== Trantor 库头文件（Drogon 底层网络库）====================
#include <trantor/utils/Date.h>
#include <trantor/utils/Logger.h>

// ==================== 后端 ORM 模型头文件 ====================
#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/OperationLogs.hpp"
#include "models/ShareFiles.hpp"
#include "models/Shares.hpp"
#include "models/Trash.hpp"
#include "models/UploadTaskChunks.hpp"
#include "models/UploadTasks.hpp"
#include "models/Users.hpp"

// ==================== Google Test 测试框架头文件 ====================
#include <gmock/gmock.h>
#include <gtest/gtest.h>
