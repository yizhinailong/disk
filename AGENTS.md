# Disk Project - Agent Guidelines

**Project**: C++23 cloud storage system (Drogon framework) | **Status**: v0.1, auth module only | **Scale**: 38 files, ~20,500 lines | **Docs**: Chinese, Code: English

## 🚨 架构约束（CRITICAL - 不可违反）

**禁止使用静态库或动态库形式！**

- ❌ **禁止创建** `add_library(STATIC/DYNAMIC)` 静态或动态库
- ✅ **必须使用** `add_executable()` 直接编译所有源文件到可执行文件（.exe）
- ❌ **禁止使用** `target_link_libraries(... disk-lib)` 链接自定义静态库
- ✅ **必须使用** `target_link_libraries(... Drogon::Drogon jwt-cpp::jwt-cpp)` 链接第三方库

**架构模式：**
```
源文件 (.cpp/.hpp) ──── 直接编译 ────> 可执行文件 (.exe)
```

**所有 CMakeLists.txt 必须遵循此规则，任何方案的实现都不得违反！**

---

## Build/Test Commands

### Configure and Build
```bash
cmake --preset linux-debug-clang      # Debug
cmake --preset linux-release-clang     # Release
cmake --build --preset linux-debug-clang
cmake --build --preset linux-debug-clang --target disk-test
```

### Run Tests
```bash
# Run all tests
ctest --preset linux-debug-clang

# Run specific test suite
ctest --preset linux-debug-clang -R PasswdHash -V
ctest --preset linux-debug-clang -R AuthRequest -V

# Run single test (CamelCase only, NO underscores)
./build/linux-debug-clang/test/disk-test --gtest_filter="PasswdHash.HashValidPassword"
./build/linux-debug-clang/test/disk-test --gtest_filter="RegisterRequest.ValidParameters"

# List all tests
./build/linux-debug-clang/test/disk-test --gtest_list_tests
```

### Format and Lint
```bash
find src test -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
clang-format -i src/controllers/AuthController.cpp
```

## Code Style Guidelines

### Type Annotations (CRITICAL - 100% Consistency)

**Pattern**: All functions use trailing return types with `auto func() -> ReturnType`

#### 1. Trailing Return Types
**ALL non-template functions** use trailing return types:
```cpp
// Service methods
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
auto Login(LoginRequest request, std::string ip_address) -> drogon::Task<Result<LoginResponse>>;
auto IsUsernameExists(std::string username) const -> drogon::Task<bool>;

// Controller methods
auto Register(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

// Utility functions
auto Hash(const std::string& password) -> Result<std::string>;
auto Verify(const std::string& password, const std::string& hash) -> bool;

// Static constexpr with noexcept
static constexpr auto GetAccessTokenExpireSeconds() noexcept -> int { return 7200; }
```

#### 2. [[nodiscard]] Attribute (Mandatory)
Applied to **ALL** methods with meaningful return values:
```cpp
// Service methods
[[nodiscard]]
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;

// Factory methods
[[nodiscard]]
static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<RegisterRequest>;

// Getters
[[nodiscard]]
auto GetJwtSecret() const -> std::string;

// Validation methods
[[nodiscard]]
auto ValidateUsername() const -> bool;

// Response builders
[[nodiscard]]
static auto Success() -> drogon::HttpResponsePtr;
[[nodiscard]]
static auto Success(const Json::Value& data) -> drogon::HttpResponsePtr;
```

#### 3. noexcept Usage (Getters and Constexpr)
- All getter methods (const methods that don't throw)
- All static constexpr methods
- ErrorInfo methods (guaranteed not to throw)
```cpp
// Static constexpr
[[nodiscard]]
static constexpr auto GetAccessTokenExpireSeconds() noexcept -> int {
    return 7200;
}

// Getter methods
[[nodiscard]]
auto HttpStatus() const noexcept -> drogon::HttpStatusCode;
[[nodiscard]]
auto CodeInt() const noexcept -> std::uint16_t;

// Config getters
[[nodiscard]]
auto GetJwtSecret() const -> std::string;
```

#### 4. std::expected<T, E> (Result<T>) Usage
**Type alias**: `using Result<T> = std::expected<T, ErrorInfo>`

Used for **all fallible operations**:
```cpp
// Type definitions (src/utils/ErrorCode.hpp)
template <typename T>
using Result = std::expected<T, ErrorInfo>;
using VoidResult = Result<void>;

// Service methods return Result<T>
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;

// Factory methods return Result<T>
static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<RegisterRequest>;

// Utility functions return Result<T>
inline auto Hash(const std::string& password) -> Result<std::string>;

// Error return pattern
co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));

// Error propagation
if (!hash_result) {
    co_return std::unexpected(hash_result.error());
}

// Value extraction
const auto& user = user_opt.value();
auto username = result->username;
```

#### 5. auto vs Explicit Types
- **auto return**: All functions use `auto func() -> ReturnType` pattern
- **Explicit types**: Member variables, parameters, struct fields
```cpp
// Function signatures (always auto with trailing return)
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;

// Member variables (explicit types)
drogon::orm::DbClientPtr m_db_client;
TokenService m_token_service;

// Struct fields (explicit types)
struct RegisterResponse {
    uint64_t id;
    std::string username;
    std::string email;
};
```

### Error Handling

#### 1. Error Code Ranges
```cpp
// 0: Success
// 10xxx: General errors (InvalidParameter, ValidationFailed, ResourceNotFound, etc.)
// 40xxx: Auth errors (UsernameExists, EmailExists, InvalidCredentials, etc.)
// 50xxx: File errors (InvalidFilename, FileNotFound, StorageQuotaExceeded, etc.)
// 60xxx: Share errors (ShareNotFound, ShareExpired, ShareAccessDenied, etc.)
```

#### 2. Error Return Patterns
```cpp
// Service layer - return errors
if (co_await IsUsernameExists(request.username)) {
    co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
}

// Controller layer - check and propagate
if (!parse_result) {
    co_return Response::Error(parse_result.error());
}

// Error propagation from other functions
if (!hash_result) {
    co_return std::unexpected(hash_result.error());
}
```

#### 3. Exception Handling (External Libraries Only)
Only catch exceptions from **external libraries** (Drogon, JWT libs), convert to Result<T>:
```cpp
// Database exception
try {
    CoroMapper<Users> mapper(m_db_client);
    user = co_await mapper.insert(user);
} catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "数据库插入失败: " << e.base().what();
    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "操作失败，请稍后重试"));
}

// JWT exception
try {
    auto decoded = jwt::decode<traits>(token);
    verifier.verify(decoded);
    return std::make_pair(user_id, username);
} catch (const jwt::error::token_verification_exception& e) {
    if (std::string(e.what()).find("expired") != std::string::npos) {
        return std::unexpected(ErrorInfo(ErrorCode::TokenExpired));
    }
    return std::unexpected(ErrorInfo(ErrorCode::InvalidToken));
}
```

### Naming Conventions

| Category | Pattern | Examples |
|----------|---------|----------|
| **Classes** | `PascalCase` | `AuthController`, `AuthService`, `TokenService`, `ConfigMgr`, `Response` |
| **Structs** | `PascalCase` | `RegisterResponse`, `LoginResponse`, `Pagination`, `ErrorInfo` |
| **Functions/Methods** | `PascalCase` | `Register()`, `Login()`, `GenerateTokens()`, `GetJwtSecret()`, `FromRequest()` |
| **Private Members** | `m_snake_case` | `m_auth_service`, `m_db_client`, `m_token_service`, `m_jwt_secret` |
| **Local Variables** | `snake_case` | `user`, `mapper`, `json`, `ip_address`, `access_token` |
| **Struct Members** | `snake_case` | `id`, `username`, `email`, `page`, `total`, `code`, `message` |
| **Constants** | `UPPER_SNAKE_CASE` | `DEFAULT_STORAGE_QUOTA`, `MIN_SECRET_LENGTH` |
| **Enum Values** | `PascalCase` | `Success`, `InvalidParameter`, `ValidationFailed`, `UsernameExists` |
| **Namespaces** | `lowercase::nested` | `disk::auth`, `disk::utils`, `disk::error`, `disk::filters` |

**Examples**:
```cpp
// Class names
class AuthController { };
class AuthService { };
class TokenService { };

// Struct names
struct RegisterResponse { };
struct LoginResponse { };
struct Pagination { };

// Function/method names
auto Register(RegisterRequest) -> drogon::Task<Result<RegisterResponse>>;
auto Login(LoginRequest) -> drogon::Task<Result<LoginResponse>>;
auto ToJson() const -> Json::Value;
auto VerifyAccessToken(const std::string& token) const -> Result<std::pair<uint64_t, std::string>>;

// Private members
drogon::orm::DbClientPtr m_db_client;
TokenService m_token_service;
std::string m_jwt_secret;

// Local variables
const auto& user = user_opt.value();
auto count = co_await mapper.count(Criteria(...));
const auto ip_address = request->getPeerAddr().toIpPort();

// Constants
static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240;
static const std::regex username_regex("^[a-zA-Z0-9_]+$");
constexpr size_t MIN_SECRET_LENGTH = 32;

// Namespaces
namespace disk::auth { }
namespace disk::utils { }
namespace disk::error { }
```

### Imports and Include Order

#### .cpp Files Pattern
```cpp
// Related header first
#include "AuthService.hpp"

// Project dependencies (if any)
#include "utils/ConfigMgr.hpp"
#include "utils/PasswdHash.hpp"
#include "services/TokenService.hpp"

// Third-party libraries (if needed)
#include <drogon/utils/Utilities.h>
#include <jwt-cpp/jwt.h>

// Standard library (if needed)
#include <string>
```

#### .hpp Files Pattern
```cpp
#pragma once

// Standard library
#include <string>
#include <memory>
#include <utility>

// Third-party libraries
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>
#include <jwt-cpp/jwt.h>

// Project headers
#include "models/Users.hpp"
#include "controllers/AuthController.hpp"
#include "utils/ErrorCode.hpp"
```

**Notes**:
- All .hpp files use `#pragma once` (not `#ifndef` guards)
- Project headers use relative paths from `src/` (e.g., `"services/AuthService.hpp"`)
- Third-party libs use angle brackets (e.g., `<drogon/HttpController.h>`)
- **Pch.hpp is defined but NOT used** - consider removing or adopting consistently

### Code Formatting

#### 1. Indentation
- **4 spaces** (no tabs)
```cpp
namespace disk::auth {
    AuthController::AuthController()
        : m_auth_service(std::make_unique<AuthService>(drogon::app().getDbClient())) {}

    auto AuthController::Register(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        LOG_INFO << "收到用户注册请求: " << request->getPeerAddr().toIpPort();
    }
}
```

#### 2. Brace Placement (K&R Style)
Opening brace on same line:
```cpp
// Functions/Methods
auto Func() -> ReturnType {
    // body
}

// Classes
class AuthController : public drogon::HttpController<AuthController> {
public:
    AuthController();
};

// Structs
struct Pagination {
    int page{ 1 };
    int page_size{ 20 };
};

// Namespaces
namespace disk::auth {
    // content
} // namespace disk::auth

// If statements
if (condition) {
    // body
}
```

#### 3. Spacing Around Operators
```cpp
// Binary operators - spaces around
response.id = user.getValueOfId();
auto status = user.getValueOfStatus();
auto attempts = user.getValueOfLoginAttempts() + 1;

// Comparison operators - spaces around
if (status == 0) {
    // ...
}

// No space after unary operators
++count;
--index;
```

#### 4. Function Parameter Formatting
```cpp
// Single line for short parameters
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
auto Hash(const std::string& password) -> Result<std::string>;

// Multi-line with trailing return type
auto AuthController::Register(drogon::HttpRequestPtr request)
    -> drogon::Task<drogon::HttpResponsePtr> {
    // body
}

// Constructor initializer list
AuthService::AuthService(drogon::orm::DbClientPtr db_client)
    : m_db_client(std::move(db_client)),
      m_token_service(ConfigMgr::GetInstance()->GetJwtSecret()) {}
```

#### 5. Structured Bindings
```cpp
// Brace initialization with dot notation
return {
    .page = page,
    .page_size = page_size,
    .total = total,
    .total_pages = page_size > 0 ? (total + page_size - 1) / page_size : 0
};

// Structured bindings with tuple return
auto [access_token, refresh_token] = m_token_service.GenerateTokens(
    user.getValueOfId(),
    user.getValueOfUsername()
);
```

### Comments and Documentation

#### 1. File Header Comments (MANDATORY)
All files must have Doxygen-style file header:
```cpp
/**
 * @file FileName.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Brief description in Chinese
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */
```

#### 2. Class/Struct Documentation
```cpp
/**
 * @brief 用户注册请求
 *
 * 验证规则：
 * - username: 4-32字符，字母数字下划线
 * - email: 有效邮箱格式
 * - password: 8-64字符，需含大小写字母和数字
 */
struct RegisterRequest {
    // ...
};
```

#### 3. Function Documentation
```cpp
/**
 * @brief 用户注册
 *
 * 业务规则：
 * - 验证用户名和邮箱的唯一性
 * - 密码使用 libsodium 的 Argon2id 算法加密存储
 * - 分配默认存储配额（10GB）
 *
 * @param request 注册请求
 * @return Result<RegisterResponse> 成功返回用户信息，失败返回错误
 */
[[nodiscard]]
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
```

#### 4. Inline Comments (Implementation)
```cpp
// 1. 检查用户名是否已存在
if (co_await IsUsernameExists(request.username)) {
    LOG_WARN << "用户名已存在: " << request.username;
    co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
}

// 2. 检查邮箱是否已存在
if (co_await IsEmailExists(request.email)) {
    LOG_WARN << "邮箱已存在: " << request.email;
    co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
}

// 3. 加密密码（使用 libsodium Argon2id）
auto hash_result = PasswdHash::Hash(request.password);
```

#### 5. Section Separators
```cpp
// ==================== 成功响应 ====================
// ==================== 错误响应 ====================
// ==================== 通用错误码 (10xxx) ====================
// ==================== Result 类型支持 ====================
```

#### 6. Member Variable Comments
```cpp
class AuthService {
private:
    drogon::orm::DbClientPtr m_db_client;                          ///< 数据库客户端
    TokenService m_token_service;                                  ///< JWT令牌服务
    static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240; ///< 默认存储配额 10GB
};
```

**Language Pattern**: Chinese for business logic, English for technical terms, English for all code identifiers.

### Coroutines (Drogon Tasks)

#### 1. Async Function Signatures
```cpp
// Service layer - returns Result<T>
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
auto Login(LoginRequest request, std::string ip_address) -> drogon::Task<Result<LoginResponse>>;
auto IsUsernameExists(std::string username) const -> drogon::Task<bool>;

// Controller layer - returns HttpResponsePtr
auto Register(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

// Filter layer - returns HttpResponsePtr (or nullptr to continue)
auto doFilter(const drogon::HttpRequestPtr& request) -> drogon::Task<drogon::HttpResponsePtr>;
```

#### 2. co_await Usage
```cpp
// Database operations (CoroMapper)
auto count = co_await mapper.count(Criteria(Users::Cols::_username, username));
user = co_await mapper.insert(user);
auto user = co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));
co_await mapper.update(user);

// Service composition (Controller calls Service)
auto register_result = co_await m_auth_service->Register(*parse_result);
auto login_result = co_await m_auth_service->Login(*parse_result, ip_address);

// Service layer composition (Service calls other Services)
if (co_await IsUsernameExists(request.username)) { ... }
auto user_opt = co_await FindUser(request.account);
```

#### 3. co_return Usage
```cpp
// Returning errors
co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));

// Returning success values
co_return response;
co_return count > 0;
co_return std::make_optional(by_username);
co_return std::nullopt;

// Controller responses
co_return Response::Error(parse_result.error());
co_return Response::Success(data);

// Filter results
co_return nullptr;  // Continue chain
co_return disk::Response::Error(disk::error::Code::TokenMissing);
```

#### 4. Coroutine Composition Example
```cpp
auto AuthService::Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>> {
    // Step 1: Check username (async)
    if (co_await IsUsernameExists(request.username)) {
        co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
    }

    // Step 2: Check email (async)
    if (co_await IsEmailExists(request.email)) {
        co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
    }

    // Step 3: Hash password (sync)
    auto hash_result = PasswdHash::Hash(request.password);
    if (!hash_result) {
        co_return std::unexpected(hash_result.error());
    }

    // Step 4: Insert to database (async)
    CoroMapper<Users> mapper(m_db_client);
    user = co_await mapper.insert(user);

    // Step 5: Return response
    co_return UserToResponse(user);
}
```

### Smart Pointers and Memory Management

#### 1. std::unique_ptr Usage
```cpp
// Member variables
std::unique_ptr<AuthService> m_auth_service;
std::unique_ptr<disk::auth::TokenService> m_token_service;

// Factory functions
std::make_unique<AuthService>(drogon::app().getDbClient())
```

#### 2. std::shared_ptr Usage
```cpp
// Singleton pattern
static std::shared_ptr<T> m_instance;

// Return from factory
return std::shared_ptr<T>(new T);
```

#### 3. Reference Passing Patterns
```cpp
// const reference for read-only parameters
auto Hash(const std::string& password) -> Result<std::string>;
auto Verify(const std::string& password, const std::string& hash) -> bool;

// move for ownership transfer
AuthService::AuthService(drogon::orm::DbClientPtr db_client)
    : m_db_client(std::move(db_client)) { }

// const reference for access
const auto& user = user_opt.value();
const auto& auth_header = request->getHeader("Authorization");
```

#### 4. Move Semantics
```cpp
// Move in constructors
AuthService::AuthService(drogon::orm::DbClientPtr db_client)
    : m_db_client(std::move(db_client)),
      m_token_service(ConfigMgr::GetInstance()->GetJwtSecret()) {}

// Move in parameters
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
// Use std::move when consuming request
auto [access_token, refresh_token] = m_token_service.GenerateTokens(
    user.getValueOfId(),
    std::move(username)  // Move instead of copy
);
```

### GTest Naming and Patterns

#### 1. Test Naming (CamelCase, NO underscores)
```cpp
// ✅ Correct (per Google Test FAQ)
TEST(PasswdHash, HashValidPassword) { }
TEST(PasswdHash, HashSamePasswordDifferentHash) { }
TEST(RegisterRequest, ValidParameters) { }
TEST(RegisterRequest, UsernameTooShort) { }
TEST(RegisterRequest, EmailValidFormat) { }

// ❌ Wrong
TEST(PasswdHash, Hash_ValidPassword) { }
TEST(RegisterRequest, valid_parameters) { }
```

**Pattern**: `TEST(SuiteName, TestCaseName)` where:
- Suite name matches the class being tested (e.g., `PasswdHash`, `RegisterRequest`)
- Test case name describes specific scenario or edge case

#### 2. ASSERT_* vs EXPECT_***
```cpp
// ASSERT_* - test cannot continue if check fails (prerequisites)
ASSERT_TRUE(result.has_value()) << "Should succeed";

// EXPECT_* - general assertions (multiple per test)
EXPECT_FALSE(result->empty()) << "Hashed password should not be empty";
EXPECT_EQ(result->username, "test_user");

// Combined pattern
if (!result.has_value()) {
    EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
}
```

#### 3. Test Organization
```
test/
├── main.cpp                    # Test entry point with global environment setup
├── utils/
│   └── PasswdHash_test.cpp     # 10 tests for password hashing
└── requests/
    └── AuthRequest_test.cpp    # 25 tests for request validation (includes controller headers)
```

#### 4. Test Helper Functions
```cpp
// Factory function for test data
static auto CreateRegisterRequest(
    const std::string& username,
    const std::string& email,
    const std::string& password
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["username"] = username;
    json["email"] = email;
    json["password"] = password;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}
```

### Constants
```cpp
// Static constexpr constants
static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240;  // 10GB

// Static const constants
static const std::regex username_regex("^[a-zA-Z0-9_]+$");
constexpr size_t MIN_SECRET_LENGTH = 32;

// Enum class values
enum class Code : std::uint16_t {
    Success = 0,
    InvalidParameter = 10001,
    ValidationFailed = 10002,
    UsernameExists = 40001,
};
```

## Project Structure
```
src/
├── controllers/   # HTTP request handlers (AuthController, includes request structs)
├── services/     # Business logic (AuthService, TokenService)
├── filters/      # Middleware (JwtAuthFilter)
├── models/       # Database models (auto-generated by drogon_ctl) - DO NOT EDIT
└── utils/        # Utilities (Pch.hpp, ErrorCode.hpp, Response.hpp, etc.)
test/             # 33 GTest unit cases
├── main.cpp       # GTest entry point
├── utils/        # Unit tests for utilities
└── requests/     # Unit tests for request validation
docs/             # 7 comprehensive design documents (Chinese)
sql/              # Database scripts (init.sql, disk.sql)
scripts/          # Build automation (auto-build.sh, rename.sh)
```

**Note**: Request and Response structs are now defined in `src/dtos/` directory following Data Transfer Object (DTO) pattern.

## Project Conventions

### Directory Layout (Deviations)
- No include/ directory - Headers co-located with .cpp files in src/ subdirectories
- Library structure: Static library `disk-lib` contains core code, main executable `disk` links against it

### Request/Response DTO 组织原则

#### 组织方式

**独立 DTOs 目录模式**：所有模块的数据传输对象（DTO）统一放在 `src/dtos/` 目录下，每个模块一个独立的 DTO 文件。

```
src/
├── dtos/                           # Data Transfer Objects (数据传输对象）
│   └── AuthDto.hpp               # 认证模块（Request + Response DTO）
├── controllers/                    # HTTP 控制器
│   └── AuthController.hpp
└── services/                       # 业务逻辑
    └── AuthService.hpp
```

#### 什么是 DTO？

**DTO（Data Transfer Object）** 是一种设计模式，用于在不同层之间传输数据：

- **Request DTO**：从 HTTP 请求解析并验证的数据结构
- **Response DTO**：业务逻辑处理后的响应数据结构

#### 命名规范

- **目录名**：`dtos/`（Data Transfer Objects 的复数形式）
- **文件名**：`{Module}Dto.hpp`（例如：`AuthDto.hpp`、`FileDto.hpp`）
- **命名空间**：使用模块命名空间（例如：`disk::auth`）
- **结构体**：
  - `{Action}Request`（例如：`RegisterRequest`）
  - `{Action}Response`（例如：`RegisterResponse`）

#### 依赖规则

```
Controller ──include──> DTOs ──include──> Service
Controller ──include──> Service
```

- ✅ 单向依赖，无循环依赖
- ✅ DTOs 作为独立的契约层
- ✅ Controller 和 Service 都依赖 DTOs

#### 验证规则

- Request DTO 必须包含 `FromRequest()` 静态工厂方法
- Request DTO 应包含私有的验证方法（如：`ValidateUsername()`）
- Response DTO 必须包含 `ToJson()` 方法
- 所有验证错误必须返回 `std::unexpected<ErrorInfo>`

#### 架构约束（CRITICAL）

**符合直接编译架构**：
- ❌ **禁止使用**静态库或动态库
- ✅ **必须使用** `add_executable()` 直接编译所有源文件到可执行文件
- ✅ DTO 文件通过 `target_sources()` 添加到可执行文件的 FILE_SET HEADERS

### Anti-Patterns (CRITICAL)
- **DO NOT EDIT** model files in src/models/ (18 auto-generated files by drogon_ctl)
- All model files contain: "DO NOT EDIT. This file is generated by drogon_ctl"
- Edit database schema (sql/init.sql) instead, then regenerate models with: `drogon_ctl create model all`

### Models Reference (src/models/)
**Auto-generated - use in services only**
- Users - Account storage (username, email, password_hash, storage_quota, status, login_attempts)
- Files - File metadata (user_id, content_id, folder_id, name, size, mime_type)
- FileContents - Deduplicated storage (hash_md5, hash_sha256, ref_count, storage_path)
- Folders - Hierarchical structure (user_id, parent_id, path, depth, item_count)
- UploadTasks - Chunked upload tracking (upload_id, file_hash, uploaded_chunks)
- Shares - File sharing (share_code, password_hash, permission, expires_at)
- ShareFiles - Polymorphic mapping (share_id, item_type, item_id)
- Trash - Recycle bin (user_id, item_type, item_id, expires_at, item_data)
- OperationLogs - Audit logging (user_id, action, target_id, details)

### Model Relationships
```
Users (1) → Files (N) → FileContents (1, deduplicated)
Users (1) → Folders (N) [self-referential parent_id]
Users (1) → Shares (N) → ShareFiles (M, polymorphic: files OR folders)
Users (1) → UploadTasks (N)
Users (1) → Trash (N)
Users (1) → OperationLogs (N)
```
- Edit database schema (sql/init.sql) instead, then regenerate models

### ORM Usage Patterns
```cpp
// In services
drogon::orm::CoroMapper<Users> mapper(m_db_client);

// Count
auto count = co_await mapper.count(Criteria(Users::Cols::_username, username));

// Find one
auto user = co_await mapper.findOne(Criteria(Users::Cols::_id, user_id));

// Insert
auto user = co_await mapper.insert(newUser);

// Update
co_await mapper.update(user);

// Remove
co_await mapper.remove(user);
```

### Unique Styles
- Bilingual: Chinese documentation, English code
- Layered architecture: Controller → Service → Model
- Precompiled header: src/utils/Pch.hpp used by all .cpp files
- Result<T> pattern: std::expected<T, ErrorInfo> for type-safe error handling

### Notes
- CMake version requirement: 4.0 (unusual - typically 3.20+)
- External Redis IP in config.json (47.97.231.191) - configure for production
- Hot-reload script: scripts/auto-build.sh uses watchexec for development

## Error Code Ranges
0: Success | 10xxx: General | 40xxx: Auth | 50xxx: Files | 60xxx: Shares
