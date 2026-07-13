# ADR-001: 从 MySQL 迁移至 PostgreSQL

## 状态

**已接受**

---

## 背景

当前网盘系统使用 MySQL 8.0 作为主数据库，通过 Drogon ORM 进行数据访问。随着功能迭代，我们在以下场景遇到 MySQL 的局限性：

1. **递归查询性能**：目录树查询使用 CTE（`WITH RECURSIVE`），在 MySQL 8.0 中执行计划不够稳定，深层嵌套目录（>10 层）时性能下降明显。
2. **JSON 操作能力**：`trash.item_data` 和 `operation_logs.details` 使用 JSON 类型存储半结构化数据。MySQL 的 JSON 函数集较基础，复杂查询需应用层处理，增加往返次数。
3. **并发写入与锁行为**：批量彻底删除时，`file_contents.ref_count` 的递减和物理清理需要精细的锁控制。MySQL InnoDB 的行锁升级行为（gap lock、next-key lock）在高并发下偶发死锁，需重试逻辑补偿。
4. **ENUM 类型演进**：当前有 3 处 ENUM（`trash.item_type`、`shares.permission`、`share_files.item_type`）。MySQL 的 ENUM 修改需 `ALTER TABLE` 重建表结构，在线 DDL 期间仍可能锁表，影响可用性。

PostgreSQL 在这些领域有成熟优势，且 Drogon ORM 已提供 PostgreSQL 支持。经过评估，我们决定将主数据库从 MySQL 迁移至 PostgreSQL 15+。

---

## 决策

**采用 PostgreSQL 15+ 替代 MySQL 8.0 作为系统主数据库。**

迁移后：
- 新部署默认使用 PostgreSQL
- 开发环境、CI、预发环境统一切换
- 现有 MySQL 生产实例进入维护模式，不再新增功能依赖

---

## 决策理由

### 1. 查询能力与性能

| 场景 | MySQL 8.0 | PostgreSQL 15+ |
|------|-----------|----------------|
| 递归 CTE | 支持，但优化器选择有限 | 原生优化更好，支持物化选项 |
| JSON 查询 | 基础函数，无索引支持 | JSONB + GIN 索引，操作符丰富 |
| 全文检索 | 需额外配置 InnoDB 全文索引 | 内置 `tsvector`，支持中文分词扩展 |
| 并行查询 | 有限支持 | 成熟的并行顺序扫描、聚合、连接 |

目录树和搜索是网盘的核心高频路径，PostgreSQL 的查询优化器在这些场景表现更稳定。

### 2. 并发控制

PostgreSQL 采用 MVCC 实现读不阻塞写、写不阻塞读，无需 MySQL 的 undo log 回滚段管理。对于网盘场景中频繁的 `ref_count` 更新和批量操作，PostgreSQL 的并发行为更可预测，死锁概率显著降低。

### 3. 类型系统与扩展性

- **ENUM**：PostgreSQL 的 `CREATE TYPE ... AS ENUM` 是独立数据库对象，修改时无需触碰数据表，不影响在线服务。
- **JSONB**：二进制存储，支持索引，更新时可局部修改，无需重写整行。
- **数组与范围类型**：为未来功能（如标签系统、配额区间管理）预留扩展空间。

### 4. 标准兼容与生态

PostgreSQL 对 SQL 标准的实现更完整，Drogon ORM 的 PostgreSQL 后端与 MySQL 后端 API 一致，迁移时上层业务代码改动可控。

---

## 备选方案评估

### 方案 A：继续使用 MySQL（不迁移）

**结论**：拒绝

- 当前问题（递归 CTE、JSON、并发锁）属于架构级限制，MySQL 9.0 在这些方面无显著改进
- 长期维护成本更高，需持续编写补偿逻辑（重试、应用层 JSON 处理）
- 与项目追求的高并发、低延迟目标存在差距

### 方案 B：使用其他数据库（SQLite、MariaDB、TiDB）

**结论**：拒绝

| 数据库 | 评估结果 |
|--------|----------|
| SQLite | 不满足单机千级并发目标，无独立服务器模式 |
| MariaDB | 与 MySQL 同源，未解决核心痛点 |
| TiDB | 分布式能力过剩，部署复杂度高，运维成本不匹配当前规模 |
| MongoDB | 文档模型与当前关系型设计冲突大，重写成本高 |

### 方案 C：分库混合（MySQL + PostgreSQL 共存）

**结论**：拒绝

- 引入两套关系型数据库增加运维和心智负担
- ORM 配置、连接池、事务管理需维护两套逻辑
- 无单一业务场景必须依赖混合架构

---

## Drogon ORM PostgreSQL 支持评估

Drogon 的 ORM 通过 `drogon::orm::DbClient` 抽象数据库访问，PostgreSQL 后端已实现以下能力：

- **连接管理**：支持 PostgreSQL 连接池，配置方式与 MySQL 一致（`config.json` 中修改 `rdbms` 和 `connection_info`）
- **模型生成**：`drogon_ctl create model` 支持 PostgreSQL 数据源，生成的模型类接口与 MySQL 版本相同
- **事务**：`DbClient` 提供 `newTransaction()`，PostgreSQL 后端完整支持
- **参数绑定**：PostgreSQL 的 `$1, $2` 占位符由 ORM 处理，但业务边界必须以与数据库推断类型宽度一致的有符号整数绑定；`BIGINT` 主键、`LIMIT` 和 `OFFSET` 使用 `int64_t`，避免 PostgreSQL 二进制参数长度不匹配
- **布尔结果**：`IS NULL`、比较表达式和其他 PostgreSQL 布尔列以 `bool` 读取，不得沿用 MySQL 风格的 `as<int>() != 0`，避免对 `t/f` 文本执行整数解析
- **协程集成**：`co_await` 异步查询在 PostgreSQL 后端同样可用

**风险点**：
- Drogon ORM 的 PostgreSQL 后端在社区中使用频率低于 MySQL，边缘场景（如特定类型映射）需额外验证
- 模型生成工具对 PostgreSQL `SERIAL` / `BIGSERIAL` 的映射需确认与当前 `AUTO_INCREMENT` 行为一致

**缓解措施**：
- 在正式迁移前，用独立分支完成模型生成和核心服务冒烟测试
- 维护一份 Drogon ORM PostgreSQL 已知问题清单，随验证进度更新

### 模型生成一致性要求

`src/models/` 必须连接当前 PostgreSQL schema 重新生成，不能只把 `model.json` 的 `rdbms` 从 MySQL 改成 PostgreSQL 而继续保留旧产物。旧模型会携带 MySQL 类型元数据和 `?` 插入占位符；在 PostgreSQL 运行时，`CoroMapper::insert()` 会直接产生语法错误，影响文件夹创建、复制、恢复、分享保存等写路径。

生成与评审时必须同时满足：

- `model.json` 使用开发 PostgreSQL 连接，并与 `config.json` 的开发数据库保持一致；生产凭据仍不得写入仓库。
- 生成模型的表名和列名使用 PostgreSQL 引号规则，参数使用 `$1`、`$2` 等占位符，插入自增表时使用 `RETURNING *`。
- PostgreSQL `BIGINT` / `INTEGER` 按生成器映射为有符号 C++ 类型；业务 DTO 和公共服务仍使用无符号 ID/字节数时，在模型边界执行显式、可审计的转换。
- 运行至少一次后端完整编译和真实 PostgreSQL 写入集成测试，不能只依赖模型文件能够单独编译。

模型方言测试应检查代表性生成产物不再包含 MySQL `bigint unsigned` / `int unsigned` 元数据，并验证 PostgreSQL 插入 SQL 的 `$n` 与 `RETURNING *` 契约。

---

## 数据类型迁移策略

### ENUM 迁移

当前 MySQL ENUM 字段：

| 表 | 字段 | 当前值 |
|----|------|--------|
| trash | item_type | `'file'`, `'folder'` |
| shares | permission | `'view'`, `'download'` |
| share_files | item_type | `'file'`, `'folder'` |

**迁移方案**：

1. 在 PostgreSQL 中创建独立 ENUM 类型：
   ```sql
   CREATE TYPE item_type AS ENUM ('file', 'folder');
   CREATE TYPE share_permission AS ENUM ('view', 'download');
   ```
2. 表字段使用这些类型替代内联 ENUM
3. 新增取值时执行 `ALTER TYPE ... ADD VALUE`，不锁表

### JSON 迁移

当前 MySQL JSON 字段：

| 表 | 字段 | 用途 |
|----|------|------|
| trash | item_data | 备份被删除项目的完整元数据 |
| operation_logs | details | 记录操作的可变细节 |

**迁移方案**：

1. 迁移至 PostgreSQL `JSONB` 类型
2. 为高频查询路径添加 GIN 索引：
   ```sql
   CREATE INDEX idx_trash_item_data ON trash USING GIN (item_data);
   ```
3. 应用层逐步将 JSON 提取逻辑下推至数据库层，减少数据往返

### 自增主键迁移

MySQL 的 `AUTO_INCREMENT` 对应 PostgreSQL 的 `BIGSERIAL` 或 `GENERATED ALWAYS AS IDENTITY`。统一采用 `BIGSERIAL`，保持与现有 `BIGINT UNSIGNED` 的语义兼容。

### 时间戳自动更新

MySQL 的 `ON UPDATE CURRENT_TIMESTAMP` 在 PostgreSQL 中无直接等价语法。通过以下方式替代：

1. 默认使用 `CURRENT_TIMESTAMP`
2. 应用层在更新操作时显式设置 `updated_at = NOW()`
3. 或创建触发器自动维护，但优先应用层控制以保持透明性

---

## 事务隔离考量

### 当前 MySQL 配置

- 默认隔离级别：`REPEATABLE READ`
- 问题：批量删除时的 gap lock 导致并发事务阻塞

### PostgreSQL 配置

- 默认隔离级别：`READ COMMITTED`
- 对于网盘场景，`READ COMMITTED` 已满足绝大多数业务需求：
  - 文件列表查询允许读取已提交的快照，无需重复读一致性
  - 上传完成时的配额扣减使用显式乐观锁或原子操作，不依赖事务隔离级别
- 对于 `ref_count` 递减和物理清理的临界操作，使用显式行级 `SELECT FOR UPDATE` 控制并发，而非依赖数据库的串行化隔离

### 隔离级别选择原则

| 场景 | 隔离级别 | 理由 |
|------|----------|------|
| 常规查询 | READ COMMITTED | 避免幻读锁开销，性能最优 |
| 配额扣减 / ref_count 更新 | READ COMMITTED + SELECT FOR UPDATE | 显式锁定，行为可控 |
| 统计报表 | REPEATABLE READ | 长事务内数据一致性 |

---

## 回滚计划

### 回滚触发条件

满足以下任一条件时，启动回滚：

1. **功能回归**：迁移后 2 周内，核心功能（上传、下载、目录树、分享）出现 3 次以上与数据库相关的线上故障
2. **性能不达标**：P99 API 响应时间较 MySQL 基线上升超过 50%，且优化后无法恢复
3. **ORM 阻塞问题**：Drogon ORM PostgreSQL 后端出现无法绕过的 bug，且社区无修复时间表
4. **运维不可接受**：备份、监控、告警的 PostgreSQL 方案在 2 周内无法稳定运行

### 回滚步骤

1. **停止写入**：切换至只读模式，返回 `503 Service Unavailable` 给非读请求
2. **数据同步**：使用逻辑备份（`pg_dump`）或 CDC 工具将 PostgreSQL 增量数据回写 MySQL
3. **切换连接**：修改 `config.json` 数据库连接信息，重启服务指向 MySQL
4. **验证**：运行冒烟测试，确认读写正常
5. **恢复服务**：解除只读模式

**回滚时间目标（RTO）**：4 小时

---

## 范围边界

### 包含在迁移范围内

- 主业务数据库（用户、文件、文件夹、分享、回收站、上传任务、操作日志）
- Drogon ORM 模型重新生成
- `config.json` 数据库连接配置更新
- SQL 初始化脚本重写为 PostgreSQL 方言
- 数据库备份策略调整（`pg_dump` / `pg_basebackup` 替代 `mysqldump`）

### 不包含在迁移范围内

- **Redis**：缓存、令牌、限流状态继续由 Redis 负责，不受本次迁移影响
- **文件存储**：`IFileStorage` / `LocalFileStorage` 的物理文件存储逻辑不变
- **业务逻辑**：服务层、控制器、DTO 的业务规则不变，仅数据访问层适配
- **客户端**：桌面客户端、API 契约不受影响
- **分片上传协议**：上传初始化、分片传输、合并流程不变

---

## 影响

### 对开发流程的影响

- 开发环境需安装 PostgreSQL 15+（替代 MySQL 8.0）
- CI 流水线更新数据库服务镜像
- 本地 `sql/init.sql` 需使用 PostgreSQL 客户端执行

### 对运维的影响

- 监控指标从 MySQL（连接数、慢查询、InnoDB 状态）切换为 PostgreSQL（连接数、锁等待、WAL、vacuum）
- 备份脚本从 `mysqldump` 切换为 `pg_dump`
- 需配置 PostgreSQL 的 `autovacuum` 策略，避免 JSONB 更新频繁导致的膨胀

### 对测试的影响

- 单元测试中的内存数据库或测试容器需切换为 PostgreSQL
- 集成测试的 `docker-compose` 或 GitHub Actions 服务定义更新

---

## 相关文档

- [系统概述](00-系统概述.md)
- [数据库设计](03-数据库设计.md)
- [部署运维指南](05-部署运维指南.md)
- Drogon ORM 文档：https://drogon.docsforge.com/master/orm-database-orm/

---

## 决策记录

| 日期 | 参与者 | 备注 |
|------|--------|------|
| 2026-05-23 | 架构评审 | 初稿评审通过，确认迁移范围与回滚条件 |

---

## 附录：术语对照

| MySQL | PostgreSQL | 说明 |
|-------|------------|------|
| `AUTO_INCREMENT` | `BIGSERIAL` | 自增主键 |
| `DATETIME` | `TIMESTAMP` | 时间戳（带时区） |
| `ENUM` | `CREATE TYPE ... AS ENUM` | 枚举类型 |
| `JSON` | `JSONB` | 二进制 JSON，支持索引 |
| `INSERT IGNORE` | `INSERT ... ON CONFLICT DO NOTHING` | 冲突忽略 |
| `utf8mb4` | `UTF8` | 字符编码（PG 默认即完整 UTF-8） |
