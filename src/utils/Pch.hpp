/**
 * @file Pch.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 预编译头文件
 * @version 0.1
 * @date 2026-01-08
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

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

// Drogon 框架头文件
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/utils/Utilities.h>

// JWT 头文件
#include <jwt-cpp/jwt.h>

// libxcrypt
#include <crypt.h>
