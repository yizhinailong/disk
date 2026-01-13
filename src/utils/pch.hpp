/**
 * @file pch.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 预编译头文件，包含常用的头文件和类型别名
 * @version 0.1
 * @date 2026-01-08
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

// Drogon 框架头文件
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>

// C++ 标准库常用头文件
#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// 项目常用头文件
#include "utils/ErrorCode.hpp"

// 错误处理
namespace Error = disk::error;
using ErrorCode = Error::ErrorCode;
template <typename T>
using Result = Error::Result<T>;
using VoidResult = Error::VoidResult;

// 项目通用类型别名
using HttpResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;
using HttpRequest = drogon::HttpRequest;
using HttpRequestPtr = drogon::HttpRequestPtr;
using HttpResponse = drogon::HttpResponse;
using HttpResponsePtr = drogon::HttpResponsePtr;
