# Disk - 高性能网盘系统

<div align="center">

[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Drogon](https://img.shields.io/badge/Drogon-1.9.11+-green.svg)](https://drogon.ws/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()

基于 Drogon Web 框架的高性能网络存储系统，提供安全、便捷的文件管理服务。

</div>

## ✨ 特性

### 🔐 安全认证
- **JWT 令牌认证** - 无状态令牌机制，Access Token (2h) + Refresh Token (7d)
- **单次使用 Refresh Token** - 每个 refresh_token 只能使用一次，刷新后旧的立即失效
- **Redis 存储** - Refresh token 存储在 Redis 中，支持高性能验证
- **Argon2id 密码哈希** - 抗暴力破解的密码存储
- **账户锁定机制** - 5次失败锁定15分钟，防止暴力攻击
- **传输加密** - 全站 HTTPS (TLS 1.3)

### 📁 文件管理
- **分片上传** - 支持大文件分片上传，默认 5MB 分片
- **断点续传** - 上传中断后可从断点处继续
- **秒传功能** - 基于文件哈希的快速上传
- **文件去重** - 内容寻址存储，相同文件只存储一份
- **批量操作** - 支持批量移动、复制、删除文件
- **文件夹管理** - 层次化目录结构，支持目录树展示

### 🔗 文件分享
- **灵活分享** - 创建分享链接，支持设置有效期
- **密码保护** - 可选的访问密码（4-8字符）
- **权限控制** - 查看/下载权限分离
- **访问统计** - 记录访问次数和下载次数

### 🗑️ 回收站
- **软删除** - 文件先移入回收站，30天后自动清理
- **恢复功能** - 可恢复到原位置或根目录
- **批量操作** - 批量恢复、批量彻底删除

### 🚀 高性能
- **异步非阻塞 I/O** - 基于 C++20 协程的高性能处理
- **并发支持** - 单服务器支持千级用户同时在线
- **数据库连接池** - 复用数据库连接，减少开销
- **Redis 缓存** - 会话管理和热点数据缓存

## 🏗️ 技术栈

| 组件 | 技术选型 | 版本 | 说明 |
|------|----------|------|------|
| **编程语言** | C++ | C++23 | 现代 C++ 特性 |
| **Web 框架** | Drogon | ≥1.9.11 | 高性能异步框架 |
| **数据库** | MySQL | ≥8.0 | 关系型数据库 |
| **缓存** | Redis | ≥6.0 | 内存数据库 |
| **ORM** | Drogon ORM | - | 自动生成模型 |
| **认证** | JWT (jwt-cpp) | ≥0.7.1 | HS256 签名 |
| **加密** | libsodium | ≥1.0.21 | Argon2id 哈希 |
| **测试** | GTest | ≥1.17.0 | 单元测试框架 |
| **构建** | CMake + vcpkg | ≥3.20 | 跨平台构建 |

## 📋 系统要求

### 最低配置
- **操作系统**: Linux (Ubuntu 22.04+) 或 Windows Server 2022+
- **编译器**: Clang 21.1.6+ 或 MSVC 19.4+
- **内存**: 8 GB RAM
- **磁盘**: 100 GB SSD
- **网络**: 100 Mbps

### 推荐配置
- **操作系统**: Linux (Ubuntu 22.04+)
- **编译器**: Clang 21.1.6+
- **内存**: 16 GB RAM
- **磁盘**: 500 GB+ SSD
- **网络**: 1 Gbps

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/yourusername/disk.git
cd disk
```

### 2. 安装依赖

#### Linux
```bash
# 安装编译工具
sudo apt-get update
sudo apt-get install -y \
    clang-21 \
    clang-format-21 \
    cmake \
    ninja-build \
    ccache

# 安装 MySQL 和 Redis
sudo apt-get install -y \
    mysql-server \
    redis-server
```

#### Windows
使用 [vcpkg](https://vcpkg.io/) 安装依赖：
```powershell
# 克隆 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat

# 安装依赖
./vcpkg install drogon[mysql,orm,redis,yaml]:x64-windows
./vcpkg install jwt-cpp:x64-windows
./vcpkg install libsodium:x64-windows
./vcpkg install gtest:x64-windows
```

### 3. 配置数据库

```bash
# 创建数据库
mysql -u root -p < sql/init.sql

# 导入存储过程（可选）
mysql -u root -p disk < sql/disk.sql
```

### 4. 配置应用

编辑 `config.json`：

```json
{
  "listeners": [
    {
      "address": "127.0.0.1",
      "port": 8080
    }
  ],
  "db_clients": [
    {
      "name": "default",
      "rdbms": "mysql",
      "host": "127.0.0.1",
      "port": 3306,
      "dbname": "disk",
      "user": "root",
      "passwd": "your_password",
      "num_connection_number": 10
    }
  ],
  "redis_clients": [
    {
      "name": "default",
      "host": "127.0.0.1",
      "port": 6379,
      "passwd": "",
      "db": 0,
      "number_of_connections": 10
    }
  ]
}
```

### 5. 配置环境变量

```bash
# 设置 JWT 密钥（生产环境必须修改）
export JWT_SECRET="your-super-secret-jwt-key-change-in-production"

# 设置 vcpkg 路径（如果未在默认位置）
export VCPKG_ROOT=/path/to/vcpkg
```

### 6. 构建项目

```bash
# 配置（Linux Debug）
cmake --preset linux-debug-clang

# 或配置（Linux Release）
cmake --preset linux-release-clang

# 构建
cmake --build --preset linux-debug-clang
```

### 7. 运行测试

```bash
# 运行所有测试
ctest --preset linux-debug-clang

# 运行特定测试套件
ctest --preset linux-debug-clang -R PasswdHash -V

# 运行单个测试
./build/linux-debug-clang/test/disk-test \
  --gtest_filter="PasswdHash.HashValidPassword"
```

### 8. 启动应用

```bash
./build/linux-debug-clang/disk
```

应用将在 `http://127.0.0.1:8080` 启动。

## 📁 项目结构

```
disk/
├── docs/                    # 项目文档
│   ├── 00-系统概述.md      # 架构、技术栈、性能指标
│   ├── 01-功能需求规格.md  # 详细功能需求
│   ├── 02-API接口设计.md   # RESTful API 规范
│   ├── 03-数据库设计.md    # ER图、表结构、索引
│   ├── 04-系统测试计划.md  # 测试策略
│   ├── 05-部署运维指南.md  # 部署配置
│   └── 06-单元测试用例.md # 测试用例文档
│
├── src/                     # 源代码
│   ├── controllers/         # HTTP 控制器
│   ├── services/            # 业务逻辑层
│   ├── filters/            # 中间件/过滤器
│   ├── models/             # 数据库模型（自动生成）
│   ├── dtos/               # 数据传输对象 (DTO)
│   │   └── AuthDto.hpp
│   └── utils/             # 工具类
│
├── test/                    # 单元测试
│   ├── utils/             # 工具类测试
│   └── requests/          # 请求验证测试
│
├── sql/                     # 数据库脚本
│   ├── init.sql           # 初始化脚本
│   └── disk.sql           # 存储过程
│
├── CMakeLists.txt          # 根构建配置
├── CMakePresets.json       # 构建预设
├── vcpkg.json             # 依赖管理
├── config.json            # 应用配置
├── AGENTS.md              # 开发规范
└── README.md              # 本文件
```

## 🔌 API 文档

### 认证接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/register` | 用户注册 |
| POST | `/api/auth/login` | 用户登录 |
| POST | `/api/auth/refresh` | 刷新令牌 |
| POST | `/api/auth/logout` | 用户登出 |

### 用户接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/user/profile` | 获取用户信息 (Bearer, 已实现) |
| PATCH | `/api/user/profile` | 更新用户信息 (Bearer, 未实现) |
| PUT | `/api/user/password` | 修改密码 (Bearer, 未实现) |
| GET | `/api/user/storage` | 获取存储空间统计 (Bearer, 未实现) |

详细的 API 文档请参考 [docs/02-API接口设计.md](docs/02-API接口设计.md)

## 🧪 测试

### 运行测试

```bash
# 运行所有测试
ctest --preset linux-debug-clang

# 显示详细输出
ctest --preset linux-debug-clang -V

# 运行特定测试套件
ctest --preset linux-debug-clang -R PasswdHash -V

# 列出所有测试
./build/linux-debug-clang/test/disk-test --gtest_list_tests
```

### 测试覆盖率

| 模块 | 测试用例 | 覆盖率 |
|------|----------|--------|
| PasswdHash | 8 | ✅ 密码哈希/验证 |
| AuthRequest | 25 | ✅ 请求验证 |

## 🛠️ 开发指南

### 代码规范

项目遵循严格的代码规范，详见 [AGENTS.md](AGENTS.md)：

**命名约定**:
- **类/结构体**: `PascalCase` (e.g., `AuthController`)
- **函数/方法**: `PascalCase` (e.g., `Register()`)
- **私有成员**: `m_` + `snake_case` (e.g., `m_auth_service`)
- **常量**: `UPPER_SNAKE_CASE` (e.g., `DEFAULT_STORAGE_QUOTA`)

**类型注解**:
```cpp
auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;
auto ValidateUsername() const -> bool;
auto GetValueOfUsername() const noexcept -> const std::string&;
```

### 代码格式化

```bash
# 格式化所有代码
find src test -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# 格式化单个文件
clang-format -i src/controllers/AuthController.cpp
```

### 添加新功能

1. **创建模型**（如需要）
   ```bash
   drogon_ctl create model table_name
   ```

2. **创建请求验证结构**
   ```cpp
   struct NewRequest {
       static auto FromRequest(const HttpRequestPtr& req) -> Result<NewRequest>;
   private:
       auto ValidateField() const -> bool;
   };
   ```

3. **创建服务**
   ```cpp
   class NewService {
       auto Method(Request request) -> drogon::Task<Result<Response>>;
   };
   ```

4. **创建控制器**
   ```cpp
   class NewController : public drogon::HttpController<NewController> {
       METHOD_LIST_BEGIN
           ADD_METHOD_TO(NewController::Method, "/api/path", drogon::Post);
       METHOD_LIST_END
   };
   ```

5. **添加测试**
   ```cpp
   TEST(NewRequest, ValidParameters) {
       auto result = NewRequest::FromRequest(req);
       EXPECT_TRUE(result.has_value());
   }
   ```

## 📚 文档

- [系统概述](docs/00-系统概述.md) - 架构、技术栈、性能指标
- [功能需求规格](docs/01-功能需求规格.md) - 详细功能需求
- [API接口设计](docs/02-API接口设计.md) - RESTful API 规范
- [数据库设计](docs/03-数据库设计.md) - ER图、表结构、索引
- [系统测试计划](docs/04-系统测试计划.md) - 测试策略
- [部署运维指南](docs/05-部署运维指南.md) - 部署配置
- [单元测试用例](docs/06-单元测试用例.md) - 测试用例文档
- [开发规范](AGENTS.md) - 代码规范和开发指南

## 🌟 项目进度

### ✅ 已完成

**后端服务 (C++/Drogon)**
- [x] 用户认证（注册、登录、登出、令牌刷新）
- [x] JWT 令牌管理（Access Token 2h + Refresh Token 7d）
- [x] Argon2id 密码哈希
- [x] 账户锁定机制（5次失败锁定15分钟）
- [x] 请求限流（登录、上传等接口）
- [x] 文件上传下载（分片上传、秒传、断点续传）
- [x] 文件夹管理（创建、目录树、面包屑）
- [x] 文件操作（重命名、移动、复制、删除）
- [x] 文件搜索
- [x] 文件分享（创建、访问、取消、下载）
- [x] 回收站功能（列表、恢复、彻底删除）
- [x] 存储配额统计
 - [x] 数据库模型生成
 - [x] 单元测试框架
 - [x] 完整的项目文档
- [x] 健康检查 API (`GET /api/health`)
- [x] 系统信息 API (`GET /api/system/info`)
- [x] 文件详情 API (`GET /api/file/{file_id}`)
- [x] 操作日志 API (`GET /api/logs`)

**TUI 客户端 (Go/Bubble Tea)**
- [x] 项目骨架和配置管理
- [x] API 客户端封装（认证、文件、文件夹、分享、回收站）
- [x] Token 加密存储
- [x] 登录界面
- [x] 文件列表界面（vim 风格导航）
- [x] 分享管理界面
- [x] 回收站界面
- [x] 上传下载框架

### ⏳ 进行中

- [ ] TUI 上传下载功能完善（分片上传、断点续传）
- [ ] TUI 设置界面
- [ ] 桌面客户端 (QT/QML) - 部分实现：7 API 类、8 ViewModel、7 服务、11 QML 视图

### 📝 计划中

- [ ] 操作日志
- [ ] 性能优化
- [ ] CDN 集成

## 🤝 贡献

欢迎贡献！请遵循以下步骤：

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

### 贡献指南

- 遵循项目的代码规范
- 为新功能添加测试
- 更新相关文档
- 确保所有测试通过

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 👥 作者

LiuFeng - [@liufeng](mailto:liufeng.code@outlook.com)

## 🙏 致谢

- [Drogon](https://drogon.ws/) - 高性能 C++ Web 框架
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) - JWT C++ 库
- [libsodium](https://doc.libsodium.org/) - 加密库
- [Google Test](https://github.com/google/googletest) - 测试框架

## 📞 联系方式

- 邮箱: liufeng.code@outlook.com
- 问题反馈: [GitHub Issues](https://github.com/yourusername/disk/issues)

---

<div align="center">

**如果觉得这个项目有帮助，请给个 Star ⭐**

Made with ❤️ by LiuFeng

</div>
