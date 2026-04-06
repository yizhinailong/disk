# 后端服务 (src/)

Drogon REST API 后端。Controller→DTO→Service 三层架构，ORM 模型自动生成。

## STRUCTURE

```
src/
├── main.cpp           # 入口：libsodium→config→TokenService→StorageMgr→ScheduledTasks→Drogon
├── controllers/       # HTTP 控制器（18文件）— 仅路由转发，不含业务逻辑
├── dtos/              # 数据传输对象（6 DTO）— 请求验证 + FromRequest 静态工厂
├── filters/           # Drogon 过滤器（4）— JwtAuth/RateLimit/ShareAuth/UploadRateLimit
├── models/            # ⚠️ ORM 自动生成（10表）— 禁止手动编辑
├── services/          # 业务逻辑层（13服务）— 所有核心业务在此
├── storage/           # 文件存储抽象—IFileStorage + LocalFileStorage + StorageMgr
└── utils/             # 工具类（8）— ConfigMgr/ErrorCode/Response/Singleton/HashUtil/Pch
```

## WHERE TO LOOK

| 任务 | 文件 |
|------|------|
| 添加新 API 端点 | 创建 Controller + DTO + Service |
| DTO 请求验证 | `dtos/*Dto.hpp` 的 `FromRequest()` 方法 |
| 错误码注册 | `utils/ErrorCode.hpp` 的 `Code` 枚举 + status_map + message_map |
| 配置项读取 | `utils/ConfigMgr.hpp` 单例 |
| 文件存储操作 | `storage/IFileStorage.hpp` 接口 + `LocalFileStorage.cpp` |
| 定时任务 | `services/ScheduledTasks.cpp` |
| Redis 操作 | `services/RedisService.cpp` |

## CONVENTIONS

- **Controller 禁含业务逻辑** — 只做参数提取→调 Service→构造响应
- **DTO 验证模式**：`static auto FromRequest(const HttpRequestPtr&) -> Result<DtoType>` 静态工厂，验证失败返回 `std::unexpected(ErrorInfo(...))`
- **Service 返回类型**：`auto Method(...) -> drogon::Task<Result<T>>`，使用 C++20 协程
- **依赖注入**：Service 通过构造函数接收 `DbClient`/其他 Service 引用

## ANTI-PATTERNS

- **禁止手动编辑 `models/`** — 用 `scripts/regenerate-models.sh` 重新生成
- **禁止 Controller 直接操作数据库** — 必须通过 Service 层
- **禁止裸 throw/try-catch** — 用 `Result<T>` + `std::unexpected(ErrorInfo(...))`
- **禁止 `std::wstring_convert` / `std::codecvt_utf8_utf16`** — C++17 弃用 API
