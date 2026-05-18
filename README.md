# Disk - 高性能网盘系统

<div align="center">

[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Drogon](https://img.shields.io/badge/Drogon-1.9.11+-green.svg)](https://drogon.ws/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()

基于 C++23 与 Drogon 的高性能网盘后端，提供用户认证、分片上传、断点续传、文件去重、目录管理、分享链接、回收站、操作日志和系统运维接口。

</div>

## 目录

- [功能概览](#功能概览)
- [技术栈](#技术栈)
- [系统要求](#系统要求)
- [快速开始](#快速开始)
- [API 总览](#api-总览)
- [项目结构](#项目结构)
- [测试](#测试)
- [开发指南](#开发指南)
- [文档](#文档)

## 功能概览

### 🔐 认证与安全

- **用户注册 / 登录 / 登出 / 令牌刷新**：支持用户名、邮箱与密码登录流程，登录后签发 Access Token 与 Refresh Token。
- **JWT Bearer 认证**：通过全局 `JwtAuthFilter` 保护用户、文件、目录、分享管理、回收站、系统信息和日志接口。
- **Refresh Token 轮换**：刷新令牌存储在 Redis 中，支持单次使用、过期控制和登出失效。
- **Argon2id 密码哈希**：使用 libsodium 进行密码哈希与校验，避免明文密码落库。
- **账户锁定与登录限流**：登录失败计数、短时锁定与 IP 级登录限流，降低暴力破解风险。
- **接口限流**：上传、回收站等高频接口可通过专用过滤器限制访问频率。
- **分享访问令牌**：公开分享通过 `X-Share-Token` 访问浏览和下载接口，避免直接暴露用户 JWT。

### 👤 用户中心

- **个人资料查询**：返回用户名、邮箱、昵称、头像、注册时间和存储空间信息。
- **个人资料更新**：支持更新昵称和头像等用户资料。
- **密码修改**：校验旧密码后重新生成 Argon2id 密码哈希。
- **容量统计**：统计已用空间、配额、文件数、目录数和分类占用情况。

### 📁 文件上传、下载与管理

- **分片上传**：默认 5MB 分片，支持初始化上传、上传分片、完成合并和取消上传。
- **断点续传**：上传初始化时返回已上传分片列表，中断后可继续上传缺失分片。
- **秒传与去重**：基于 MD5 内容哈希查找已有内容，相同文件只保留一份物理对象。
- **后台合并**：通过 `AssemblyWorkerPool` 控制分片合并并发和缓冲区大小。
- **上传任务缓存**：为分片热路径维护短期任务缓存，减少重复查询。
- **容量预检查**：上传前校验用户剩余配额，避免超额写入。
- **文件列表**：支持分页、目录过滤、文件/文件夹类型过滤、排序字段和排序方向。
- **文件详情**：查询单个文件/目录元数据与路径面包屑。
- **文件下载**：提供下载信息预检和文件流式下载，支持 HTTP Range 断点下载。
- **重命名**：校验文件名长度、非法字符、保留名称和同目录冲突。
- **移动 / 复制 / 删除**：支持批量移动、复制和软删除；复制复用内容对象并维护引用计数。
- **搜索**：支持按关键词、类型、目录范围和分页条件搜索文件或目录。

### 📂 目录管理

- **创建目录**：支持在指定父目录下创建文件夹，并校验同级重名。
- **目录树**：返回用户目录层级树，便于前端构建侧边栏或选择器。
- **面包屑路径**：返回从根目录到指定目录的路径链路。

### 🔗 文件分享

- **创建分享**：支持选择文件/目录生成分享链接，可设置有效期、访问密码和权限。
- **我的分享列表**：支持分页查看自己创建的分享，并按状态过滤。
- **分享详情**：查看分享基础信息和关联文件列表。
- **分享更新**：可修改过期时间、访问密码和查看/下载权限。
- **批量取消分享**：将分享状态置为取消，立即停止公开访问。
- **公开访问校验**：公开接口校验分享密码，并生成短期分享访问令牌。
- **分享浏览**：通过分享令牌浏览分享内容，支持目录导航和面包屑。
- **分享下载**：通过分享令牌下载共享文件，并统计下载次数。

### 🗑️ 回收站

- **软删除**：普通删除会将文件移入回收站，保留恢复所需元数据。
- **回收站列表**：分页查看已删除文件，默认按删除时间排序。
- **批量恢复**：支持恢复多个条目；遇到命名冲突时自动生成新名称，原目录不存在时回退到根目录。
- **批量彻底删除**：永久删除回收站条目，减少引用计数并释放用户配额。
- **清空回收站**：一次性删除当前用户全部回收站内容，返回删除数量和释放空间。
- **自动清理**：定时清理过期回收站项和过期上传任务，引用计数归零后删除物理文件。

### 💾 存储与后端能力

- **内容寻址存储**：本地存储按内容哈希组织目录和文件名，方便去重与校验。
- **存储抽象层**：通过 `IFileStorage` 定义文件保存、读取、合并、清理等操作，当前实现为 `LocalFileStorage`。
- **引用计数**：同一内容可被多个用户文件记录引用，复制和彻底删除会维护 `ref_count`。
- **MySQL 数据持久化**：用户、文件、内容、上传任务、分享、回收站和操作日志存储在 MySQL。
- **Redis 缓存与状态管理**：用于刷新令牌、黑名单、登录失败计数、分享访问和限流状态。
- **统一响应模型**：业务层使用 `Result<T>` 返回成功或错误，控制器统一转换为 HTTP JSON 响应。
- **协程异步服务**：服务层基于 `drogon::Task<Result<T>>` 实现非阻塞业务流程。

### 📊 运维、日志与可观测性

- **健康检查**：公开接口检查服务状态、数据库和 Redis 连接状态。
- **系统信息**：认证后查看版本、构建信息、运行状态和存储能力等系统信息。
- **操作日志**：记录并分页查询用户上传、下载、删除、分享等关键操作。
- **访问日志插件**：通过 Drogon `AccessLogger` 输出请求日志，支持日志大小限制和轮转。

## 技术栈

| 组件 | 技术选型 | 版本 | 说明 |
|------|----------|------|------|
| 编程语言 | C++ | C++23 | 协程、`std::expected` 等现代 C++ 能力 |
| Web 框架 | Drogon | ≥ 1.9.11 | 高性能异步 HTTP 框架 |
| 数据库 | MySQL | ≥ 8.0 | 业务数据持久化 |
| 缓存 | Redis | ≥ 6.0 | Token、限流、临时状态 |
| ORM | Drogon ORM | - | 由模型配置生成数据库模型 |
| 认证 | jwt-cpp | ≥ 0.7.1 | JWT HS256 签名与校验 |
| 密码哈希 | libsodium | ≥ 1.0.21 | Argon2id 密码存储 |
| 测试 | GoogleTest | ≥ 1.17.0 | 单元测试与服务测试 |
| 构建 | CMake + vcpkg | CMake 4.0+ | 跨平台依赖和构建管理 |

## 系统要求

### 最低配置

- **操作系统**：Linux (Ubuntu 22.04+) 或 Windows Server 2022+
- **编译器**：Clang 21.1.6+ 或 MSVC/clang-cl 19.4+
- **构建工具**：CMake 4.0+、Ninja、vcpkg
- **运行依赖**：MySQL 8.0+、Redis 6.0+
- **内存**：8 GB RAM
- **磁盘**：100 GB SSD

### 推荐配置

- **操作系统**：Linux (Ubuntu 22.04+)
- **编译器**：Clang 21.1.6+
- **内存**：16 GB RAM
- **磁盘**：500 GB+ SSD
- **网络**：1 Gbps

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/yourusername/disk.git
cd disk
```

### 2. 准备依赖

#### Linux

```bash
sudo apt-get update
sudo apt-get install -y clang cmake ninja-build ccache mysql-server redis-server
```

#### Windows

项目使用 vcpkg manifest 管理 C++ 依赖。请先安装 vcpkg，并设置 `VCPKG_ROOT`：

```powershell
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

### 3. 初始化数据库

```bash
mysql -u root -p < sql/init.sql
```

### 4. 配置应用

编辑 `config.json` 中的数据库、Redis 和存储路径。核心配置示例：

```json
{
  "listeners": [{ "address": "127.0.0.1", "port": 8080 }],
  "custom_config": {
    "disk": {
      "storage_base_path": "build/uploaded",
      "temp_upload_path": "build/temp_uploads",
      "chunk_size": 5242880,
      "max_file_size": 10737418240,
      "upload_task_expiry_seconds": 86400,
      "upload_rate_limit_per_minute": 240,
      "assembly_max_concurrent": 4,
      "assemble_buffer_size_bytes": 262144
    }
  }
}
```

### 5. 配置环境变量

```bash
export JWT_SECRET="your-super-secret-jwt-key-change-in-production"
export VCPKG_ROOT=/path/to/vcpkg
```

> `CMakePresets.json` 为开发构建提供了默认 `JWT_SECRET`，生产环境必须改为安全随机密钥。

### 6. 构建项目

```bash
cmake --preset linux-debug-clang
cmake --build --preset linux-debug-clang
```

Windows 可使用：

```powershell
cmake --preset windows-debug-clang-cl
cmake --build --preset windows-debug-clang-cl
```

### 7. 启动应用

```bash
./build/linux-debug-clang/src/disk
```

默认监听地址为 `http://127.0.0.1:8080`。

## API 总览

全局认证由 `config.json` 中的 `drogon::plugin::GlobalFilters` 配置。除注册、登录、刷新令牌、健康检查和部分公开分享接口外，其余接口默认需要 `Authorization: Bearer <access_token>`。

### 认证接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| POST | `/api/auth/register` | 用户注册 | 公开 |
| POST | `/api/auth/login` | 用户登录，返回 Access/Refresh Token | 公开 |
| POST | `/api/auth/refresh` | 使用 Refresh Token 刷新令牌 | 公开 |
| POST | `/api/auth/logout` | 登出并使令牌失效 | JWT |

### 用户接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/user/profile` | 获取当前用户资料 | JWT |
| PATCH | `/api/user/profile` | 更新昵称、头像等资料 | JWT |
| PUT | `/api/user/password` | 修改密码 | JWT |
| GET | `/api/user/storage` | 获取容量和文件统计 | JWT |

### 文件接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| POST | `/api/file/upload/init` | 初始化上传，支持秒传和断点续传信息 | JWT + 上传限流 |
| POST | `/api/file/upload/chunk` | 上传文件分片 | JWT + 上传限流 |
| POST | `/api/file/upload/complete` | 完成上传并合并分片 | JWT + 上传限流 |
| DELETE | `/api/file/upload/{upload_id}` | 取消上传任务 | JWT + 上传限流 |
| GET | `/api/file/list` | 分页列出文件和目录 | JWT |
| GET | `/api/file/{file_id}` | 获取文件或目录详情 | JWT |
| GET | `/api/file/download/{file_id}/info` | 获取下载前置元数据 | JWT |
| GET | `/api/file/download/{file_id}` | 下载文件，支持 Range | JWT |
| PUT | `/api/file/{file_id}/rename` | 重命名文件或目录 | JWT |
| PUT | `/api/file/move` | 批量移动文件或目录 | JWT |
| POST | `/api/file/copy` | 批量复制文件或目录 | JWT |
| DELETE | `/api/file` | 批量软删除到回收站 | JWT |
| GET | `/api/file/search` | 搜索文件或目录 | JWT |

### 目录接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| POST | `/api/folder/create` | 创建目录 | JWT |
| GET | `/api/folder/tree` | 获取目录树 | JWT |
| GET | `/api/folder/{folder_id}/breadcrumb` | 获取目录面包屑 | JWT |

### 分享接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| POST | `/api/share` | 创建分享 | JWT |
| GET | `/api/share` | 获取我的分享列表 | JWT |
| GET | `/api/share/{share_id}` | 获取分享详情 | JWT |
| PUT | `/api/share/{share_id}` | 更新分享配置 | JWT |
| DELETE | `/api/share` | 批量取消分享 | JWT |
| POST | `/api/share/access/{share_id}` | 校验分享密码并获取分享令牌 | 公开 |
| GET | `/api/share/browse/{share_id}` | 浏览分享内容 | Share Token |
| GET | `/api/share/download/{share_id}/{file_id}` | 下载分享文件 | Share Token |

### 回收站接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/trash` | 分页查看回收站 | JWT + 限流 |
| POST | `/api/trash/restore` | 批量恢复回收站条目 | JWT + 限流 |
| DELETE | `/api/trash` | 批量彻底删除 | JWT + 限流 |
| DELETE | `/api/trash/all` | 清空回收站 | JWT + 限流 |

### 系统与日志接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/health` | 健康检查 | 公开 |
| GET | `/api/system/info` | 获取系统信息 | JWT |
| GET | `/api/logs` | 分页查看当前用户操作日志 | JWT |

### 管理员接口

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|------|
| GET | `/api/admin/users` | 用户列表 | JWT + Admin |
| GET | `/api/admin/users/{id}` | 用户详情 | JWT + Admin |
| PUT | `/api/admin/users/{id}/status` | 修改用户状态 | JWT + Admin |
| PUT | `/api/admin/users/{id}/role` | 修改用户角色 | JWT + Admin |
| DELETE | `/api/admin/users/{id}` | 删除用户 | JWT + Admin |
| GET | `/api/admin/storage/stats` | 全局存储统计 | JWT + Admin |
| GET | `/api/admin/shares` | 分享列表 | JWT + Admin |
| GET | `/api/admin/shares/{id}` | 分享详情 | JWT + Admin |
| DELETE | `/api/admin/shares/{id}` | 强制取消分享 | JWT + Admin |
| GET | `/api/admin/stats/overview` | 概览统计 | JWT + Admin |
| GET | `/api/admin/stats/system` | 系统状态 | JWT + Admin |

所有管理员接口需要 JWT 角色为管理员 (role=1)，由 AdminAuthFilter 校验。

更详细的请求和响应结构请参考 [docs/design/02-API接口设计.md](docs/design/02-API接口设计.md)。

## 项目结构

```text
disk/
├── src/
│   ├── controllers/    # HTTP 控制器，只负责解析请求和返回响应
│   ├── services/       # 业务逻辑层，基于 drogon::Task<Result<T>>
│   ├── models/         # Drogon ORM 模型
│   ├── dtos/           # 请求/响应 DTO 与参数校验
│   ├── filters/        # JWT、分享令牌、限流等过滤器
│   ├── storage/        # 存储抽象、本地存储与分片合并
│   └── utils/          # 配置、错误码、响应、哈希、单例等工具
├── test/
│   ├── services/       # 服务层单元测试
│   ├── dtos/           # DTO 校验测试
│   ├── filters/        # 过滤器测试
│   ├── storage/        # 存储与合并测试
│   └── integration/    # 需要运行服务的集成测试
├── docs/design/        # 中文设计文档
├── sql/init.sql        # MySQL 初始化脚本
├── config.json         # Drogon 运行配置
├── CMakePresets.json   # Linux/Windows 构建预设
└── vcpkg.json          # C++ 依赖清单
```

## 测试

### 运行测试

```bash
# 运行所有 CTest 注册测试
ctest --preset linux-debug-clang

# 显示详细输出
ctest --preset linux-debug-clang -V

# 运行指定测试
ctest --preset linux-debug-clang -R UploadFlow -V

# 直接运行 GTest 并列出测试套件
./build/linux-debug-clang/test/disk-test --gtest_list_tests
```

### 覆盖模块

- **工具与基础设施**：配置读取、哈希、Redis Key、统一响应、密码哈希。
- **认证与用户**：Token 生命周期、撤销、登录限流、用户资料、密码修改、容量统计。
- **文件服务**：上传一致性、完成上传并发保护、复制/删除原子性、批量操作、搜索、下载流程。
- **分享服务**：创建、查询、更新、取消、访问、浏览、下载与批量处理。
- **回收站与清理**：列表、恢复、彻底删除、清空、过期清理。
- **过滤器与存储**：JWT、分享令牌、上传限流、分片合并工作池。
- **集成测试**：认证生命周期、上传下载、目录生命周期、文件元数据、文件变更、分享管理、回收站生命周期、系统信息和健康检查。

## 开发指南

### 代码规范

项目遵循 [AGENTS.md](AGENTS.md) 中的约定：

- **类/结构体**：`PascalCase`，例如 `AuthController`。
- **函数/方法**：`PascalCase`，例如 `Register()`。
- **私有成员**：`m_` + `snake_case`，例如 `m_auth_service`。
- **常量**：`UPPER_SNAKE_CASE`，例如 `DEFAULT_STORAGE_QUOTA`。
- **业务错误**：使用 `Result<T>` / `ErrorInfo` 返回，不通过异常控制业务流程。
- **控制器职责**：控制器不写业务逻辑，只调用服务并转换响应。
- **模型扩展**：Drogon ORM 生成的 `.hpp` 不手写业务代码，自定义逻辑放在 `.cpp`。

### 添加新功能建议流程

1. 在 `docs/design/02-API接口设计.md` 中补充或更新接口设计。
2. 在 `src/dtos/` 添加请求/响应 DTO 和参数校验。
3. 在 `src/services/` 实现业务逻辑，返回 `drogon::Task<Result<T>>`。
4. 在 `src/controllers/` 添加路由和控制器方法。
5. 如需开放匿名访问，更新 `config.json` 中的全局过滤器豁免规则。
6. 在 `test/` 中添加单元测试或集成测试，并运行相关测试。

### 格式化

```bash
find src test -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

## 文档

- [系统概述](docs/design/00-系统概述.md) - 架构、技术栈、性能指标
- [功能需求规格](docs/design/01-功能需求规格.md) - 详细功能需求
- [API接口设计](docs/design/02-API接口设计.md) - RESTful API 规范
- [数据库设计](docs/design/03-数据库设计.md) - ER 图、表结构、索引
- [系统测试计划](docs/design/04-系统测试计划.md) - 测试策略
- [部署运维指南](docs/design/05-部署运维指南.md) - 部署配置
- [单元测试用例](docs/design/06-单元测试用例.md) - 测试用例文档
- [开发规范](AGENTS.md) - 项目约定和开发指南

## 贡献

欢迎贡献功能、测试和文档改进：

1. Fork 本仓库
2. 创建特性分支：`git checkout -b feature/your-feature`
3. 提交更改并确保测试通过
4. 推送分支并创建 Pull Request

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

## 作者

LiuFeng - [liufeng.code@outlook.com](mailto:liufeng.code@outlook.com)

## 致谢

- [Drogon](https://drogon.ws/) - 高性能 C++ Web 框架
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) - JWT C++ 库
- [libsodium](https://doc.libsodium.org/) - 密码哈希与加密基础库
- [GoogleTest](https://github.com/google/googletest) - C++ 测试框架

---

<div align="center">

**如果觉得这个项目有帮助，请给个 Star ⭐**

</div>
