# PROJECT KNOWLEDGE BASE

**Generated:** 2026-04-06T10:46:52Z
**Commit:** f51ccbb config: update redis host to localhost
**Branch:** main

## OVERVIEW

高性能网盘系统 — C++23 Drogon 后端 + Qt6/QML 桌面客户端。MySQL + Redis 存储，JWT 认证，分片上传，文件分享。

## STRUCTURE

```
disk/
├── src/              # Drogon REST API 后端（controllers/services/models/dtos/filters/storage）
├── ui/diskqml/       # Qt6/QML 桌面客户端（api/services/viewmodels/qml）
├── test/             # GTest 单元测试（dtos/services/filters/utils/mocks）
├── tests/api/        # Bash API 集成测试
├── sql/              # 数据库初始化脚本 (init.sql)
├── scripts/          # 构建/维护脚本
├── docs/             # 设计文档 + UI 设计规范
├── config.json       # Drogon 运行时配置（数据库/Redis/插件/上传）
├── CMakePresets.json # CMake 预设（Linux/Win, Debug/Release, Clang）
└── vcpkg.json        # 依赖：drogon[mysql,orm,redis,yaml] jwt-cpp libsodium gtest
```

## WHERE TO LOOK

| 任务 | 位置 | 说明 |
|------|------|------|
| 添加 REST API | `src/controllers/` + `src/dtos/` + `src/services/` | Controller→DTO→Service 三层 |
| 修改业务逻辑 | `src/services/` | 所有业务逻辑在此层 |
| 添加/修改数据库模型 | `src/models/` | ⚠️ 自动生成，用 `scripts/regenerate-models.sh` |
| 认证/鉴权 | `src/filters/JwtAuthFilter.cpp` + `src/services/TokenService.cpp` | JWT + Redis refresh token |
| 错误码定义 | `src/utils/ErrorCode.hpp` | `Result<T>` = `std::expected<T, ErrorInfo>` |
| 统一响应格式 | `src/utils/Response.hpp` | `Response::Success/Error/Paginated/FromResult` |
| 配置读取 | `src/utils/ConfigMgr.hpp` | 单例，从 config.json 加载 |
| QML 客户端页面 | `ui/diskqml/qml/views/` | 各业务页面 QML 文件 |
| ViewModel 层 | `ui/diskqml/src/viewmodels/` | QML 与 C++ 的桥梁 |
| API 客户端封装 | `ui/diskqml/src/api/` | HTTP 请求封装 |
| 数据库 Schema | `sql/init.sql` | 10 张表，含索引和约束 |
| API 设计文档 | `docs/design/02-API接口设计.md` | RESTful API 完整规范 |
| 代码注释规范 | `docs/07-代码注释规范.md` | 中文 Doxygen 注释规范 |

## CODE MAP

| 符号 | 类型 | 位置 | 角色 |
|------|------|------|------|
| `disk::error::Result<T>` | alias | `src/utils/ErrorCode.hpp` | `std::expected<T, ErrorInfo>` 错误处理核心 |
| `disk::Response` | class | `src/utils/Response.hpp` | 统一 API 响应构造器 |
| `disk::error::Code` | enum | `src/utils/ErrorCode.hpp` | 错误码枚举 (0/10xxx/40xxx/50xxx/60xxx/70xxx) |
| `disk::error::ErrorInfo` | struct | `src/utils/ErrorCode.hpp` | 错误码+消息+HTTP状态 |
| `disk::utils::ConfigMgr` | class | `src/utils/ConfigMgr.hpp` | 运行时配置单例 |
| `disk::utils::Singleton` | class | `src/utils/Singleton.hpp` | CRTP 单例模板 |
| `disk::services::TokenService` | class | `src/services/TokenService.hpp` | JWT 令牌签发/验证 |
| `disk::services::AuthService` | class | `src/services/AuthService.hpp` | 注册/登录/登出 |
| `disk::services::FileService` | class | `src/services/FileService.hpp` | 文件上传/下载/秒传/去重 |
| `disk::storage::IFileStorage` | interface | `src/storage/IFileStorage.hpp` | 文件存储抽象接口 |
| `disk::storage::StorageMgr` | class | `src/storage/StorageMgr.hpp` | 文件存储管理器 |
| `disk::filters::JwtAuthFilter` | class | `src/filters/JwtAuthFilter.hpp` | JWT 认证过滤器 |
| `disk::filters::RateLimitFilter` | class | `src/filters/RateLimitFilter.hpp` | 请求限流过滤器 |

## CONVENTIONS

- **C++23 严格模式**：CMAKE_CXX_STANDARD_REQUIRED=ON, 扩展关闭
- **命名**：类/函数 `PascalCase`，私有成员 `m_` 前缀，常量 `UPPER_SNAKE_CASE`
- **返回类型**：统一使用尾置返回 `auto Method() -> ReturnType;`
- **[[nodiscard]]**：纯函数/返回值必须标记
- **错误处理**：所有业务函数返回 `Result<T>`（= `std::expected<T, ErrorInfo>`），禁止异常用于业务流
- **注释语言**：所有 Doxygen 注释必须中文（`docs/07-代码注释规范.md` 有46个标准术语）
- **响应格式**：`{"code": 0, "message": "...", "data": ...}` 统一 JSON
- **ColumnLimit: 0**：clang-format 不限制行宽
- **BinPackArguments/Parameters: false**：参数每行一个
- **基于 Google style 的自定义花括号**：函数/控制语句后不换行，除空函数体外
- **ORM 模型**：Drogon 自动生成，禁止手动编辑 `src/models/`
- **Controller→DTO→Service 三层**：Controller 不含业务逻辑，DTO 负责验证，Service 负责业务
- **Namespace 层次**：`disk::controllers`, `disk::services`, `disk::filters`, `disk::models`, `disk::storage`, `disk::utils`

## ANTI-PATTERNS

- **禁止手动编辑 `src/models/`** — 自动生成文件，修改用 `scripts/regenerate-models.sh`
- **禁止使用 `std::wstring_convert`, `std::codecvt_utf8_utf16`** — C++17 弃用 API，模型脚本自动修补
- **禁止在注释中使用非标准英文术语** — 用 `docs/07-代码注释规范.md` 定义的中文术语
- **禁止 `trailing return type` 以外的函数声明风格** — 项目统一尾置返回
- **禁止裸 throw/try-catch 处理业务错误** — 使用 `Result<T>` + `std::unexpected(ErrorInfo(...))`
- **文件名禁止字符**：`/ \ : * ? " < > |` 及控制字符（DTO 验证层强制）
- **搜索关键词禁止**：`% _ \ ' "` （SQL 注入防护）

## UNIQUE STYLES

- **双测试目录**：`test/` = C++ GTest 单元测试，`tests/` = Bash API 集成测试
- **DTO 验证模式**：每个 DTO 有 `static auto FromRequest(const HttpRequestPtr&) -> Result<DtoType>` 静态工厂
- **Monorepo 但无前端/后端分离**：后端在根 `src/`，前端在 `ui/diskqml/`，各有独立 CMakeLists
- **Token 双令牌**：Access Token 2h + Refresh Token 7d，Redis 存储单次使用
- **内容寻址存储**：相同文件哈希只存一份，支持秒传
- **TDD 测试文档**：部分测试文件包含 RED/GREEN/REFACTOR 阶段记录

## COMMANDS

```bash
# 配置 (Linux Debug)
cmake --preset linux-debug-clang

# 构建
cmake --build --preset linux-debug-clang

# 运行所有测试
ctest --preset linux-debug-clang

# 运行特定测试
ctest --preset linux-debug-clang -R PasswdHash -V

# 单个 GTest 过滤
./build/linux-debug-clang/test/disk-test --gtest_filter="PasswdHash.HashValidPassword"

# 格式化代码
find src test ui -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# 重新生成 ORM 模型
./scripts/regenerate-models.sh

# 检查模型同步
./scripts/check-model-list.sh

# 自动构建（watchexec 守护）
./scripts/auto-build.sh

# 初始化数据库
mysql -u root -p < sql/init.sql

# API 集成测试
bash tests/api/run_all.sh
```

## NOTES

- 启动顺序：`sql/init.sql` → `config.json` → `src/main.cpp` → 初始化 libsodium → 加载配置 → 初始化 TokenService → 初始化文件存储 → 注册定时任务 → `drogon::app().run()`
- `config.json` 中的 `custom_config.disk` 包含文件存储配置（路径/分片大小/最大文件）
- 全局过滤器通过 Drogon 插件 `GlobalFilters` 注册（JwtAuthFilter + RateLimitFilter），exempt 路径在 config.json 配置
- JWT_SECRET 必须通过环境变量设置，至少32字符
- 默认管理员账号：admin / Admin123（在 sql/init.sql 中）
- QML 客户端架构：`diskqml_core` 静态库（非 QML 组件）+ `appdiskqml` 可执行（QML 组件+视图）
- QML 中 `QML_ELEMENT` 的类不能加入 `diskqml_core`，只能加入 `appdiskqml` 的 QML module
