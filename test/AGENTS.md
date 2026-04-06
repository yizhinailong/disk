# 单元测试 (test/)

Google Test (GTest) C++ 单元测试。Mock 对象隔离外部依赖。

## STRUCTURE

```
test/
├── CMakeLists.txt     # disk-test 可执行，gtest_discover_tests 自动发现
├── main.cpp           # 全局环境：初始化 libsodium
├── dtos/              # DTO 验证测试（6）：Auth/File/Folder/Share/Trash/User DTO
├── services/          # Service 层测试（13）：Token/Auth/File/Share/Trash/Redis/Cleanup/Hash 等
├── filters/           # 过滤器测试（2）：JwtAuth/ShareAuth
├── utils/             # 工具测试（5）：PasswdHash/ConfigMgr/Response/FileHash/RedisKeyPrefix
├── mocks/             # Mock 对象（2）：MockDbClient/MockRedisClient
└── integration/       # Shell 集成测试
```

## WHERE TO LOOK

| 任务 | 位置 |
|------|------|
| 添加 Service 测试 | `services/{ServiceName}_test.cpp` |
| 添加 DTO 测试 | `dtos/{DtoName}_test.cpp` |
| Mock 数据库 | `mocks/MockDbClient.hpp` — `CreateMockDbClient()` |
| Mock Redis | `mocks/MockRedisClient.hpp` — `CreateMockRedisClient()` |

## CONVENTIONS

- **文件命名**：`{Component}_test.cpp`
- **测试命名**：`TEST(ClassName, TestDescription)`
- **断言模式**：`Result<T>` 用 `has_value()` 检查成功，`error().code` 检查错误码
- **Mock 工厂**：`CreateMockDbClient()` / `CreateMockRedisClient()` 返回智能指针
- **TDD 文档**：部分文件包含 RED/GREEN/REFACTOR 阶段注释

## COMMANDS

```bash
# 运行全部
ctest --preset linux-debug-clang

# 过滤运行
ctest --preset linux-debug-clang -R TokenService -V

# 单个测试
./build/linux-debug-clang/test/disk-test --gtest_filter="TokenService.*"
```
