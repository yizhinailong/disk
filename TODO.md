# 分布式后端重构 TODO

> 状态：实施中
>
> 目标：将当前 Drogon 后端重构为可无粘性会话横向扩容、可故障恢复的模块化单体。
>
> 原则：本文件是执行索引，不替代 `docs/design/` 中的权威设计。每一阶段必须先更新对应设计/API/数据库/部署/测试文档，再修改代码。
>
> 最近验证（2026-07-21，本轮 Phase 6 PostgreSQL 读副本准入评估）：`cmake --preset linux-debug-clang`、完整构建和 RuntimeConfig/拓扑聚焦 CTest 10/10 均通过；完整 CTest 共 1391 项，1385 通过、6 项按环境门控跳过（`promtool`、3 项显式 S3/MinIO 门控、2 项显式分布式拓扑门控），0 失败，总耗时 485.33 秒；OpenSpec 严格校验 24/24 通过。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；当前主机没有 Nginx、Docker 或其他容器运行时，目标环境还须执行真实 `nginx -t`、证书链、双 API 随机路由和压力门禁。

## 1. 目标与范围

### 1.1 第一阶段目标

- [ ] 至少两个 Drogon API 实例可同时对外服务，负载均衡不依赖 sticky session。
- [ ] 同一上传任务的任意分片、完成、取消请求可以到达任意 API 实例。
- [ ] 任一 API 或 Worker 在上传过程中退出后，客户端重试可以继续或恢复任务。
- [ ] PostgreSQL 是持久业务状态和状态迁移的唯一事实来源。
- [ ] Redis 只保存可重建的缓存、令牌状态和限流状态，不承担上传事务的唯一所有权。
- [ ] 最终 Blob 与上传暂存均使用所有实例可访问的对象存储。
- [ ] 定时清理和异步存储操作支持多 Worker 竞争、租约过期接管和幂等重试。
- [ ] 保持现有桌面客户端和 REST API 兼容；必须改变的合同通过文档、版本化和迁移期明确发布。

### 1.2 暂不包含

- [ ] 不在本轮拆分 Auth、File、Share 等独立微服务。
- [ ] 不在本轮引入分库分表或分布式 SQL 数据库。
- [ ] 不让客户端直接依赖某一家云厂商的专有接口。
- [ ] 不以共享本地目录或负载均衡粘性会话作为最终架构。
- [ ] 不同时维护一套永久的旧上传实现和一套新实现；迁移兼容代码必须有删除任务和截止条件。

## 2. 当前基线与主要差距

| 领域 | 当前状态 | 分布式差距 | 目标状态 |
|---|---|---|---|
| 元数据 | PostgreSQL | 缺少上传完成租约、持久任务队列和正式迁移流程 | PostgreSQL CAS 状态机、租约、可回放任务 |
| 缓存/令牌 | Redis + 进程内短缓存 | 本地负缓存和上传任务缓存跨实例不一致 | 本地缓存仅作提示，写操作始终由共享状态校验 |
| 最终 Blob | local 或 S3/MinIO，默认 local | local 后端不能供多实例共享 | 生产统一使用 S3/MinIO |
| 上传暂存 | local 与 S3 后端均使用本地目录 | 分片被路由到不同节点后无法组装 | 对象存储原生暂存，节点无关 |
| 上传完成所有权 | PostgreSQL 条件更新 + 有期限租约 | 目标环境多实例接管仍待验收 | 数据库是唯一所有权来源；本机只做无任务标识的并发限流 |
| 定时任务 | 每个 API 实例 `runEvery` | 多实例会重复扫描和执行 | 独立 Worker + 数据库任务认领 |
| 健康检查 | PostgreSQL + Redis | 缺少对象存储、角色和就绪状态 | liveness/readiness 分离并覆盖必要依赖 |
| 集成测试 | 单后端、共享状态、串行 | 未验证随机路由、宕机和接管 | 双实例 + MinIO + 故障注入场景 |

## 3. 目标架构

```text
Desktop / Other Clients
          |
    L7 Load Balancer
          |
   +------+------+
   |             |
API Instance A  API Instance B ... N
   |             |
   +------+------+-------------------+
          |          |               |
     PostgreSQL    Redis          S3/MinIO
          |                          |
          +------- Worker A/B -------+
```

### 3.1 必须保持的系统不变量

- [ ] 上传、取消、过期、完成的状态迁移只能通过数据库条件更新完成。
- [ ] 所有租约时间使用 PostgreSQL `NOW()`，不依赖实例本地时钟判断所有权。
- [ ] 不在数据库事务中等待大文件网络 I/O 或对象存储长操作。
- [ ] 每个外部副作用都必须可幂等重试，或有持久补偿/对账任务。
- [ ] 已完成请求的重复调用返回同一业务结果，不重复创建文件、不重复结算配额。
- [x] 最终 Blob 的删除必须基于持久化零引用判断，失败时进入重试队列；禁止竞态下直接补偿删除共享内容对象。
- [ ] 进程内缓存不得作为授权、配额、上传状态或任务所有权的最终依据。
- [ ] 上传暂存不得要求同一 `upload_id` 的请求命中同一实例。
- [ ] API 实例退出不得丢失待执行任务；Worker 退出后租约到期可由其他 Worker 接管。
- [ ] 生产密钥只从环境变量或密钥管理系统注入。

## 4. 架构决策门

以下决策必须先写入新的分布式架构 ADR，再开始代码改造。

- [x] **D-01 上传暂存格式**：采用 `staging/{upload_id}/chunks/{chunk_index}` 不可变分片对象；数据库保存对象 key、大小、MD5、ETag。完成阶段按顺序流式读取并组装，避免节点本地盘依赖。
- [x] **D-02 完成接口语义**：第一期保持现有同步 `complete` API；通过数据库租约保证单飞，实例失败后允许重试接管。异步 `202 + status` 只作为后续版本化能力。
- [x] **D-03 持久任务实现**：优先使用 PostgreSQL 任务表和 `FOR UPDATE SKIP LOCKED`，避免为持久业务状态同时依赖 Redis 锁与数据库事务。
- [x] **D-04 进程角色**：同一二进制支持明确的 `api`、`worker`、`all` 角色；生产分别部署 API 与 Worker，本地开发可使用 `all`。
- [x] **D-05 Blob 寻址**：新 Blob 使用 SHA-256 内容寻址；旧 MD5 对象继续按数据库 `storage_path` 读取，并通过独立迁移工具搬迁。
- [x] **D-06 完整性校验**：对象组装过程继续计算并验证整文件 MD5 与 SHA-256，不能把 multipart ETag 当作文件 MD5。
- [x] **D-07 Redis 高可用**：第一期使用托管或 Sentinel 代理提供的稳定写入端点；完成拓扑、`SCAN`、批量删除和 Lua/CAS 验证前不支持 Redis Cluster。
- [x] **D-08 部署平台**：代码保持编排中立；Docker Compose 用于本地多实例验收，Kubernetes 作为参考生产部署并承载探针、滚动发布和自动扩容。
- [x] **D-09 一致性窗口**：撤销校验不缓存“未撤销”结果，只正缓存已撤销状态；Redis 安全校验失败采用 fail closed。

## 5. Phase 0：文档、基线与验收口径

### 5.1 权威文档先行

- [x] 新增 `docs/design/ADR-002-分布式后端与上传状态机.md`，记录上述决策、备选方案和取舍。
- [x] 更新 `docs/design/00-系统概述.md`，区分当前单机能力和重构后的多实例能力，修正过早的“无状态”表述。
- [x] 更新 `docs/design/02-API接口设计.md`，定义重复上传分片、重复完成、完成中、租约接管、取消冲突和重试响应。
- [x] 更新 `docs/design/03-数据库设计.md`，加入上传状态机、租约字段、分片元数据和持久任务表。
- [x] 更新 `docs/design/04-系统测试计划.md`，加入双实例、随机路由、故障恢复和对象存储一致性测试。
- [x] 更新 `docs/design/05-部署运维指南.md`，加入 API/Worker 角色、负载均衡、PostgreSQL/Redis/S3 高可用和滚动发布。
- [x] 更新 `docs/design/06-单元测试用例.md`，列出状态机、租约、任务认领、幂等与重试用例。
- [x] 更新 `docs/design/07-压力测试.md`，增加集群吞吐、对象存储带宽、任务积压和故障期间性能测试。
- [x] 若桌面端可观察到新的完成中/重试状态，先更新对应 `docs/desktop/` 权威文档。

### 5.2 建立基线

- [x] 记录当前完整构建与测试结果，保存失败项和环境前提。
- [x] 记录上传初始化、单分片、完成、下载 Range、取消和清理的延迟/吞吐基线。
- [x] 记录 PostgreSQL 连接数、Redis 命令量、文件 I/O 线程和组装并发基线。
- [x] 明确生产目标 SLO：可用性、上传成功率、P95/P99 延迟、恢复时间、可接受孤儿对象清理时限。
- [x] 建立故障矩阵：API 退出、Worker 退出、数据库短暂不可用、Redis 不可用、S3 超时、S3 成功但 DB 失败、DB 成功但清理失败。
- [x] 盘点所有进程内可变状态和 `runEvery`/`runAfter` 定时器，标明“本地维护”或“集群唯一业务任务”。
- [ ] 盘点当前生产数据量、进行中上传数和 local Blob 使用情况，决定是否需要存量 Blob 迁移工具。

### 5.3 Phase 0 验收

- [x] ADR 已批准，所有跨模块合同均有唯一文档来源。
- [x] 基线指标和目标 SLO 可复现。
- [x] 每个故障场景都有预期状态、重试责任方和恢复方式。

## 6. Phase 1：数据库迁移与上传状态模型

### 6.1 建立迁移机制

- [x] 确认并固定数据库 schema migration 工具与目录，不再只依赖全量 `sql/init.sql` 初始化已有数据库。
- [x] 每次迁移提供 expand、数据回填、contract 三阶段计划；滚动发布期间新旧版本必须兼容。
- [x] 迁移命令由单独部署任务执行，禁止所有 API 实例启动时并发迁移。
- [x] 为全新安装同步更新 `sql/init.sql`，但不把它当作线上升级脚本。
- [x] 增加迁移前后 schema 验证和重复执行测试。

### 6.2 扩展 `upload_tasks`

- [x] 在保留既有状态值的前提下增加 `Finalizing`，评估是否需要终态 `Failed`。
- [x] 增加 `state_version BIGINT NOT NULL DEFAULT 0`，用于乐观并发控制和诊断。
- [x] 增加 `lease_owner`、`lease_expires_at`，用于完成任务认领和崩溃接管。
- [x] 增加 `finalize_attempts`、`last_error_code`、`last_error_at`，用于有限重试和运维诊断。
- [x] 增加 `completed_file_id`，使重复 `complete` 可以返回同一文件结果。
- [x] 增加 `staging_backend`、`staging_prefix`，支持迁移期间按任务选择 local/S3 暂存。
- [x] 按最终设计增加 multipart/staging object 标识，禁止把凭据写入数据库。
- [x] 为状态、过期时间、租约到期和待恢复任务建立必要索引。
- [x] 增加 CHECK/FK 约束，禁止非法状态与字段组合。

### 6.3 扩展 `upload_task_chunks`

- [x] 继续以该表作为唯一上传进度来源，不向 `upload_tasks` 添加兼容 JSON 分片字段。
- [x] 增加 `size_bytes`、`hash_md5`、`object_key`、`etag` 等恢复组装所需元数据。
- [x] 保持 `(task_id, chunk_index)` 唯一，并校验索引和大小边界。
- [x] 设计“对象已写但 DB 未记录”与“DB 已记录但对象缺失”的对账和修复语义。
- [x] 禁止终态任务继续写入分片记录；使用带任务状态条件的数据库写入原语。

### 6.4 增加持久任务表

- [x] 增加 `storage_jobs` 或等价表，字段至少包含任务类型、聚合 ID、去重键、状态、尝试次数、可执行时间、租约所有者、租约截止、错误和时间戳。
- [x] 为去重键增加唯一约束，确保同一清理/删除任务只产生一个逻辑任务。
- [x] 为 `pending/retry + available_at` 和 `running + lease_expires_at` 建立认领索引。
- [x] 定义任务状态迁移：`Pending -> Running -> Succeeded`，失败进入 `Retry`，超过上限进入 `DeadLetter`。
- [x] 定义 dead-letter 查询、人工重试和审计流程。

### 6.5 Phase 1 验收

- [x] 新 schema 可从空库创建，也可从当前 schema 无损升级。
- [x] 旧版本可在 expand 阶段继续运行；contract 迁移在旧版本完全退出后执行。
- [x] 所有新增索引通过 `EXPLAIN` 验证目标查询。
- [x] 迁移回滚策略和不可逆步骤已写入部署文档。

## 7. Phase 2：跨实例状态机、幂等与事务正确性

### 7.1 上传状态机

- [x] 用独立领域类型集中定义允许的状态迁移，禁止在多个服务中散落魔法状态值。
- [x] 实现 `InProgress -> Finalizing` 条件认领：校验用户、完整分片覆盖、状态和租约，并原子写入 owner/version/expiry。
- [x] `Finalizing` 且租约有效时，其他完成请求返回文档定义的可重试结果。
- [x] `Finalizing` 且租约过期时，允许新实例通过 CAS 接管并增加尝试次数。
- [x] `Completed` 的重复完成请求通过 `completed_file_id` 返回原文件。
- [x] `Cancelled/Expired/Failed` 的重复请求具有稳定且文档化的结果。
- [x] 续租使用数据库时间并校验 `lease_owner + state_version`，旧 owner 不得覆盖新 owner。
- [x] API/Worker 优雅退出时停止认领新任务；已认领任务完成或由租约超时接管。

### 7.2 分片写入正确性

- [x] 分片对象 key 包含 `upload_id`、索引和已验证的内容标识，重复写入同一内容必须幂等。
- [x] 写对象前校验几何信息、用户和任务期限；写入后的 DB 记录必须再次以 `InProgress` 为条件。
- [x] 处理取消/完成与在途分片写入竞态：迟到分片不能改变终态，只能成为可清理孤儿对象。
- [x] 本地上传任务缓存只缓存不可变几何信息；不得跳过终态条件写或所有权检查。
- [x] 分片 DB 元数据与对象 HEAD 结果不一致时，完成操作必须失败并触发对账，不能生成损坏文件。

### 7.3 完成上传 Saga

- [x] 完成流程固定为：校验覆盖 -> CAS 认领 -> 外部组装/校验 -> 短事务提交元数据 -> 持久化清理任务。
- [x] 对象存储组装、哈希和复制不得占用长数据库事务。
- [x] 组装时按 chunk index 流式读取，限制缓冲区，不把整文件加载进内存。
- [x] 同时校验总大小、整文件 MD5 和 SHA-256；不信任客户端 hash 或 S3 ETag。
- [x] 最终 Blob 创建必须幂等；内容去重使用数据库唯一约束和原子 upsert，而不是“先查再插”作为唯一防线。
- [x] 文件名冲突依赖数据库唯一约束给出确定的领域错误。
- [x] 最终短事务原子完成：内容引用、文件记录、文件夹计数、reserved-to-used 配额、上传终态、`completed_file_id` 和清理任务入队。
- [x] 事务提交失败后保留可识别的 staging 对象，由持久任务对账清理；禁止无引用复核就删除共享最终 Blob。
- [x] 完成响应丢失后，客户端重复调用可由数据库恢复原响应。

### 7.4 取消与过期

- [x] 修正取消流程：只有成功将 `InProgress -> Cancelled` 的事务赢家才能在同一事务内释放 reserved quota。
- [x] 取消事务同时删除/标记分片元数据并写入 staging cleanup 任务，实际对象删除在事务后执行。
- [x] 过期流程继续使用条件更新，并与配额释放、分片元数据处理、清理任务入队放入同一事务。
- [x] 明确定义 `Finalizing` 状态是否允许取消；推荐返回冲突并让完成/恢复流程收敛。
- [x] 所有清理操作可重复执行，对“对象不存在”按成功处理。

### 7.5 Blob 引用与删除

- [x] 内容创建/引用增加在数据库层原子化，覆盖两个上传同时命中相同内容的情况。
- [x] Blob 删除改为事务内产生持久任务，Worker 删除前再次确认内容不存在或 `ref_count == 0`。
- [x] Blob 删除成功后再完成任务；超时和临时错误指数退避重试。
- [x] 增加孤儿 staging、孤儿 final Blob、零引用内容记录和缺失 Blob 的对账作业。
- [x] 为每种不一致定义自动修复、告警或人工介入策略。

### 7.6 Phase 2 验收

- [x] 100 个并发重复 `complete` 最多生成一个文件记录、一次配额结算和一个内容引用增量。
- [x] 并发 `complete/cancel/expire` 最终只有一个合法终态。
- [ ] 任意步骤重试不会造成负配额、重复文件、错误 ref_count 或误删 Blob。
- [ ] 所有状态机和事务竞态有数据库级集成测试，不只依赖源码字符串断言。

## 8. Phase 3：对象存储原生上传暂存

### 8.1 存储接口重构

- [x] 修改现有 `UploadStagingStorage`，使描述符不再以 `std::filesystem::path` 代表所有后端。
- [x] 定义后端无关的上传会话、分片对象和组装结果描述符。
- [x] 将“分片暂存”“组装/流式读取”“最终 Blob 提升”职责保持清晰，不把 HTTP、数据库或权限逻辑放进存储层。
- [x] 保留 local 实现用于开发和单机测试，但生产多实例路径不得调用本地暂存。
- [ ] 迁移完成后删除仅为旧流程服务的重复接口和死代码。

### 8.2 扩展 S3 客户端能力

- [x] 支持上传分片对象、HEAD、流式/Range 读取、幂等删除和批量清理。
- [x] 按 D-01/D-06 的设计支持流式组装至临时完整对象或 multipart 目标。
- [x] 支持大对象的 server-side multipart copy/promote，不能假设单次 `CopyObject` 可覆盖所有文件大小。
- [x] 所有阻塞 AWS SDK 调用继续运行在专用工作线程，不阻塞 Drogon event loop。
- [x] 为超时、限流、5xx、连接失败建立分类重试；认证和参数错误不做无界重试。
- [x] multipart 流程在请求内失败时执行 `AbortMultipartUpload`。
- [x] multipart 在进程退出或 Abort 失败后由持久任务与 bucket 生命周期规则兜底。
- [x] 不记录 access key、secret、session token 或带签名 URL。

### 8.3 对象 key 与生命周期

- [x] 规范 staging、assembled、final 三类 key 前缀并进行严格输入规范化，禁止路径穿越。
- [x] staging key 必须按 `upload_id` 隔离，清理任务不得使用未经验证的宽泛前缀。
- [x] final key 与 D-05 内容寻址决策一致，数据库 `storage_path` 仍为读取权威。
- [x] 在 MinIO/AWS S3 配置 staging/multipart 生命周期过期规则，作为应用清理失败的最后保护。
- [x] final 前缀不得配置自动过期规则。

### 8.4 下载路径

- [x] 保持现有 Range、ETag、权限和分享下载合同。
- [ ] 验证每个 API 实例都能从 S3/MinIO 读取任意 Blob，不依赖本地缓存文件。
- [x] 对对象缺失、长度不一致和 Range 上游中断返回一致错误并记录可对账信息。
- [ ] 评估后续增加短期签名下载 URL，但不作为本轮多实例上线前置条件。

### 8.5 Phase 3 验收

- [ ] 初始化、所有分片和完成请求逐次轮询到不同实例仍能成功。
- [ ] API 节点删除本地 `temp_upload_path` 后，S3 暂存流程不受影响。
- [ ] 大于单次 copy 限制的文件可完成组装/提升，内存占用保持有界。
- [ ] 对象存储故障不会留下无法识别或无法清理的 multipart/staging 工件。

## 9. Phase 4：Worker、调度与故障恢复

### 9.1 进程角色

- [x] 增加明确配置，如 `DISK_PROCESS_ROLE=api|worker|all`，默认值和安全模式行为写入部署文档。
- [x] API 角色不注册集群级清理任务；Worker 角色不监听公开业务端口，或只暴露内部健康/指标端口。
- [x] 本地开发 `all` 模式明确标记为单进程便利模式，不用于多副本生产部署。
- [x] 将 `ScheduledTasks` 的领域清理职责迁移到 Worker；令牌本地缓存驱逐等纯进程维护仍留在所属进程。

### 9.2 任务认领与执行

- [x] 使用 `FOR UPDATE SKIP LOCKED` 批量认领到期任务，并写入 `locked_by/locked_until`。
- [x] Worker 使用稳定且唯一的 instance ID，日志和指标均携带该 ID。
- [x] 长任务周期续租；续租和完成都必须校验当前 owner/version。
- [x] Worker 崩溃后，其他 Worker 可在租约到期后接管。
- [x] 任务 handler 按类型注册，至少覆盖 staging cleanup、multipart abort、Blob GC、过期上传和一致性对账。
- [x] 临时错误采用带抖动的指数退避，永久错误进入 dead-letter。
- [x] 限制单 Worker 并发和每类任务并发，避免对象存储或数据库被恢复任务压垮。
- [x] 优雅关闭时停止认领、等待有界时间、停止续租并让未完成任务自然接管。

### 9.3 周期任务唯一触发

- [x] 周期扫描使用 PostgreSQL advisory lock、周期任务唯一行或等价机制，确保一个周期只产生一次逻辑扫描。
- [x] 扫描本身必须分页、有上限且可继续，避免一小时任务重叠。
- [x] 即使多个 Worker 同时触发，任务去重键仍能阻止重复副作用。
- [x] 记录每轮扫描开始、结束、耗时、候选数、成功数、失败数和下一游标。

### 9.4 Phase 4 验收

- [x] 两个 Worker 并发运行时，每个逻辑任务只成功执行一次，允许幂等重复尝试。
- [x] 杀死持有租约的 Worker 后，任务在约定恢复时间内被接管。
- [x] API 扩容或缩容不会改变周期任务执行次数。
- [x] dead-letter 可查询、告警、人工重放并保留审计信息。

### 9.5 Phase 4 Worker 多实例验收记录（2026-07-21）

`test_worker_drain_takeover.py` 在唯一临时 PostgreSQL 上启动真实 Worker B/C，并分别用数据库行锁阻塞两个零引用 Blob GC handler。两个单并发实例同时领取后，测试要求两行同时处于 `Running/attempts=1`、owner 精确分离为 B/C；释放锁后，两项任务均一次进入 `Succeeded`，清空 owner/租约，保留唯一去重行，并各自只删除一次内容行和 Blob。测试不直接修改任务状态、owner、attempts 或租约截止时间。

同一脚本先让 Worker A 持有受阻任务并接收 `SIGTERM`，验证 readiness 退出、停止领取新任务和超时排空后保留有效租约，再由 Worker B 严格晚于 PostgreSQL 持久租约截止接管，attempts 从 1 收敛到 2。`test_blob_gc_process_death.py` 另以真实 `SIGKILL` 终止租约持有者，验证后继 Worker 只在租约到期后接管，并将队列与 Blob GC 副作用原子收敛。

队列事务、真实 `SIGKILL`、排空接管与拓扑契约聚焦 CTest 4/4 通过；完整 CTest 共 1383 项，1377 通过、6 项按既有环境门控跳过、0 失败，总耗时 427.86 秒；OpenSpec 严格校验 24/24 通过。本记录关闭 Phase 4 的 Worker 队列唯一执行与进程死亡接管验收，不替代 Phase 6 目标高可用环境、Phase 9 灰度迁移及最终 DoD 的真实 MinIO/多实例/压力门禁。

## 10. Phase 5：缓存、认证与跨实例一致性

### 10.1 上传和业务缓存

- [x] 审计所有 `unordered_map`/本地缓存，区分不可变加速数据与业务状态。
- [x] 上传任务本地缓存不得缓存可决定写入合法性的终态；写路径使用数据库条件原语。
- [x] 文件列表等 Redis 缓存保持共享 key 规范，确保所有写操作覆盖相关失效路径。
- [x] 评估 `DeleteByPrefix/SCAN` 在目标 Redis HA 方案中的行为和性能；文件列表改用每用户版本化 key，并删除无生产调用的前缀扫描实现。
- [x] 所有缓存 miss/error 都有明确降级策略；Redis 不可用时不得静默绕过安全限流或令牌撤销要求。

### 10.2 令牌撤销

- [x] 按 D-09 明确 access token 和 share token 的最大撤销传播延迟：Redis 撤销写入成功后，其他实例的下一次校验立即生效。
- [x] 移除跨实例不可靠的 access/share token negative cache，进程内只正缓存已撤销结果。
- [x] 不保留有界负缓存；API、ADR 和测试合同统一为零负缓存窗口。
- [x] 登出、刷新令牌 CAS、分享取消在两个实例间进行交叉验证。
- [x] Redis 故障策略已写入安全设计并实现：撤销校验 fail closed，缓存/限流按合同降级，并完成双实例故障注入验收。

### 10.3 Phase 5 验收

- [x] 在实例 A 登录/登出/取消分享后，实例 B 的行为符合已批准的一致性窗口。
- [x] 本地缓存清空、实例重启或缓存内容不同不会改变最终业务结果。
- [x] Redis 故障和恢复期间没有绕过认证、重复 refresh 或永久脏缓存。

### 10.4 Phase 5 跨实例令牌一致性验收记录（2026-07-21）

`test_auth_cluster_consistency.py` 使用唯一临时 PostgreSQL 启动两个真实 API，并让两实例经本测试独占的可切断 TCP 代理访问同一 Redis；它不停止或清空共享 Redis 服务。A 注册/登录后，B 可立即使用 owner token；同一 refresh token 并发提交到 A/B 时，Redis CAS 在故障前和恢复后都只选出一个赢家，失败方和旧 token 重放均被拒绝。

A 取消分享后，B 立即拒绝此前签发的 Share Token；A 登出后，B 立即以 `40111` 拒绝旧 access token。随后真实终止并重新启动 B，清空全部进程内正撤销缓存，重启后的 B 仍从共享 Redis 与 PostgreSQL 得到相同拒绝结果，同时未取消分享的 token 保持可用。

故障阶段代理主动切断两个 API 的既有 Redis 连接：两实例 liveness 保持 200、readiness 精确因 Redis 返回 503，owner/share token 校验和并发 refresh 均返回 `70002`，且 refresh 赢家为零。代理恢复后原 API 进程无需重启即可重新 ready，live owner/share token 恢复正常；故障前的 refresh token 仍只成功轮换一次，既有撤销状态也未丢失。证据文件以 `0600` 原子发布且不含任何可重放 token 或密码。

聚焦 CTest 115/115 通过；完整 CTest 共 1384 项，1378 通过、6 项按既有环境门控跳过、0 失败，总耗时 456.94 秒；OpenSpec 严格校验 24/24 通过。本记录关闭 Phase 5 的本机双 API 认证一致性验收，但不替代 Phase 6 目标 Redis HA 端点故障切换、Phase 9 预发布灰度及最终 DoD 的真实拓扑演练。

## 11. Phase 6：部署、高可用与容量治理

### 11.1 可重复环境

- [x] 提供 `docker-compose.distributed.yml`：PostgreSQL、Redis、MinIO、两个 API、两个 Worker 和无粘性负载均衡器；当前主机无 Docker，真实启动验收仍由 Phase 6/8 验收项跟踪。
- [x] 四个应用实例使用同一 `disk-distributed:local` 镜像，通过 `DISK_PROCESS_ROLE` 和 `DISK_INSTANCE_ID` 区分职责。
- [x] 分布式 JSON 的密码字段为空；S3、数据库、Redis 密钥只从环境或 Secret 注入，占位 `.env.distributed` 不入库。
- [x] 启动校验覆盖 role、S3 staging/final 组合、bucket、endpoint scheme、TLS 校验和成对 S3 凭据。

### 11.2 PostgreSQL

- [x] 采用主库写入端点和经过演练的故障切换方案；第一期不做业务分片。
- [x] 本地双 API/双 Worker 固定 PostgreSQL 32、Redis 16 条应用连接预算并保留运维余量；扩容公式已写入部署指南。
- [ ] 区分事务池模式限制，确认 Drogon ORM、prepared statement 和 advisory lock 兼容性。
- [x] Compose PostgreSQL 显式设置 statement、lock、idle transaction 超时，避免故障任务长期占用资源。
- [x] 建立备份、时间点恢复和恢复演练流程。
- [x] 只在可证明安全的只读查询上评估读副本；当前查询白名单为空，上传状态、权限及全部既有业务/运维判断只读主库写端点。

### 11.3 Redis

- [ ] 使用私网高可用端点并开启认证/TLS（目标环境支持时）。
- [x] 验证故障切换期间连接重建、命令超时、Lua/CAS、SCAN 和 key TTL 行为。
- [x] 分布式配置默认每实例 4 条 Redis 连接，双 API/双 Worker 总预算 16 条；扩容必须重新核算。
- [x] 备份或持久化要求按 refresh token/撤销语义明确，不把 Redis 当作可随意清空的纯缓存。

### 11.4 S3/MinIO

- [ ] 生产 bucket 开启必要的版本、加密、TLS、最小权限和生命周期规则。
- [ ] API/Worker 凭据仅允许所需前缀和操作，迁移工具使用独立临时权限。
- [x] 配置连接池、请求超时、重试预算和每实例并发上限。
- [ ] 评估 MinIO 自建部署的磁盘冗余、节点故障域和备份；不能用单节点 MinIO 宣称存储高可用。

### 11.5 负载均衡与发布

- [x] Nginx 采用无 cookie/ip-hash 的 `least_conn`，覆盖并传递客户端 IP、协议和代理生成的请求 ID。
- [x] Nginx 固定 20 MiB 请求体、330 秒转发超时、关闭请求缓冲，且未启用 `non_idempotent` 重放。
- [x] API/Worker 支持 SIGTERM 优雅关闭和有界 drain。
- [ ] 数据库迁移、API、Worker 按兼容顺序滚动发布。
- [ ] 配置最小副本、反亲和/故障域、资源 requests/limits 和扩缩容指标。
- [x] 保留现有 `/api/health` 兼容行为，新增或明确 liveness/readiness 端点。
- [x] readiness 覆盖当前角色必需的 PostgreSQL、Redis、S3 和初始化状态；liveness 不因短暂外部依赖故障反复重启进程。

### 11.6 Phase 6 验收

- [ ] 关闭任一 API 实例，现有连接在允许范围内失败且重试后恢复，新请求继续成功。
- [ ] 关闭任一 Worker，任务由其他 Worker 接管。
- [ ] PostgreSQL、Redis 和对象存储的故障切换均完成演练并记录 RTO/RPO。
- [ ] 扩容 API 不需要迁移本地文件、复制会话或修改负载均衡粘性规则。

### 11.7 Phase 6 Redis 会话安全状态持久化验收记录（2026-07-21）

设计合同现将 Redis 状态分为两类：文件列表、版本键和限流窗口按各自合同重建或降级；`refresh_token:*` 当前哈希、`access_token_blacklist:*` 和 `share_token_blacklist:*` 是带绝对过期时间的会话安全状态。受支持的常规重启/无损故障切换必须保留已确认写入和原到期时间；若目标恢复无法证明安全键完整，则保持认证入口关闭，轮换 JWT Secret、重启全部 API，并确认所有旧 access/refresh/share token 失效后才能切流。

`test_redis_session_persistence.py` 使用唯一临时 PostgreSQL、两个真实 API 和测试专属 Redis/Valkey 持久目录。夹具通过真实登录/登出写入 refresh 当前哈希和 access 撤销，确认 `appendonly=yes`、`appendfsync=always` 及 `WAITAOF` 本地 fsync 屏障后 `SIGKILL` Redis；从同一目录启动新 Redis 进程时，两安全键的值不变、`PTTL` 保持为正且严格递减。

故障期间 API A 进程 PID 保持不变，liveness 为 200、readiness 因 Redis 为 503；恢复后 A 无需重启即可重新 ready。恢复后才启动的冷 API B 没有进程内撤销缓存，仍从持久 Redis 以 `401 + 40111` 拒绝旧 access token；A/B 对故障前 refresh token 并发轮换只产生一个赢家，旧 token 重放失败。测试不连接、不停止也不清空共享 Redis，`0600` 证据不含 token、密码、JTI 或 Redis key。

认证/Redis 聚焦 CTest 62/62 通过；完整 CTest 共 1385 项，1379 通过、6 项既有环境门控跳过、0 失败，总耗时 452.44 秒；OpenSpec 严格校验 24/24 通过。本记录关闭 Redis 持久化语义与同一持久卷进程崩溃门禁，但不替代目标高可用端点的复制、自动故障切换、TLS/认证、RTO/RPO 和全会话失效演练，因此 11.3 的目标环境 HA 两项及 11.6 故障切换验收继续保持未勾选。

### 11.8 Phase 6 Redis 稳定写端点故障切换验收记录（2026-07-21）

设计合同固定应用只连接单一稳定 Redis 写端点：控制面必须先隔离旧主、确认安全状态到达副本，再提升副本并切换端点。切换窗口内既有连接按命令超时失败，owner/share 校验和 refresh 必须 fail closed；端点恢复后原 API 进程重建连接。refresh Lua/CAS 仍只允许一个赢家，撤销状态和绝对到期时间不得重置；跨节点 `SCAN` 游标不可续用，运维扫描必须从新主游标 `0` 重新开始。

`test_redis_failover_semantics.py` 使用唯一临时 PostgreSQL、两个真实 API、两个独立持久目录的真实 Redis/Valkey 主从和测试专属稳定 TCP 端点。副本追平真实 refresh、access 撤销与 16 个带 TTL 的扫描夹具后，代理保留既有 TCP 连接但暂停转发；两 API 均在 1.019 至 1.020 秒内返回 `500 + 70002`，同时 liveness 200、readiness 503。夹具随后 `SIGKILL` 旧主、提升副本并切换稳定端点，原双 API PID 不变并在 2.024 秒内恢复 ready。

提升后 refresh/撤销值保持不变且 `PTTL` 为正并严格递减；冷撤销路径继续以 `401 + 40111` 拒绝旧 access token，A/B 并发轮换 refresh 仍只有一个 CAS 赢家，旧 token 重放失败；从新主游标 `0` 重启 `SCAN` 精确得到 16 个夹具键。`0600` 原子证据 `.sisyphus/evidence/redis-failover-semantics-summary.json` 的 SHA-256 为 `555fbac3d2964980bad2ed4ec55045135034ee36c2feb2f61b5ec2085b1dabe7`；它不含 token、密码、JTI、Redis key 名或值，也不连接、停止或清空共享 Redis 服务。

认证/Redis 聚焦 CTest 3/3 通过；完整 CTest 共 1386 项，1380 通过、6 项既有环境门控跳过、0 失败，总耗时 468.43 秒；OpenSpec 严格校验 24/24 通过。本记录关闭 11.3 的连接重建、命令超时、Lua/CAS、SCAN 与 TTL 本机语义门禁，但同主机手工提升不替代目标私网 HA 端点、Sentinel/托管控制面、认证/TLS、独立故障域和 RTO/RPO 演练，因此 11.3 的私网 HA 项与 11.6 的完整故障切换验收继续保持未勾选。

### 11.9 Phase 6 PostgreSQL 稳定写端点故障切换验收记录（2026-07-21）

设计合同固定 API、Worker 和 migration Job 只连接单一私网 PostgreSQL 主库写端点；第一期不做业务分片，也不让上传状态、授权、配额、租约或任务认领读取异步副本。切换必须先停止新写入或进入受控故障窗口，隔离旧主并确认候选已重放要求保留的 WAL，再提升候选、切换稳定端点并关闭旧连接；提交结果未知的非幂等写不得由代理或应用自动重放。

`test_postgres_failover_semantics.py` 使用两个独立数据目录建立真实 PostgreSQL 物理流复制，并让两个真实 API 经测试专属 TCP 稳定端点连接旧主。热备确认只读且追平真实注册数据后，夹具暂停既有连接；两个 profile 读取和一次资料写入均在 1.019 至 1.020 秒内返回 `500 + 10006`，同时 liveness 200、readiness 503 且只把 database 标为 unhealthy。夹具随后 `SIGKILL` 旧主、提升热备并切换端点，原双 API PID 不变并在 1.030 秒内恢复 ready。

提升后 PostgreSQL system identifier 保持不变、timeline 前进，切换前用户与昵称基线无损，超时写未在新主重放；API B 的提升后资料更新可由 API A 立即读到。`0600` 原子证据 `.sisyphus/evidence/postgres-failover-semantics-summary.json` 的 SHA-256 为 `048157ff16d30013ee476f902ad6bdd7ccc96059b0f81a1f4707150ebfbe0e53`，且不含密码、令牌、连接串或业务标识。

PostgreSQL/认证/Redis/拓扑聚焦 CTest 5/5 通过（48.17 秒）；完整 CTest 共 1387 项，1381 通过、6 项既有环境门控跳过、0 失败，总耗时 478.86 秒；OpenSpec 严格校验 24/24 通过。本记录关闭 11.2 的稳定主库写端点、本地物理副本提升和第一期不分片语义门禁，但同主机手工提升不替代目标托管/Patroni/HAProxy 控制面、TLS、独立故障域、备份/PITR 或经批准的 RTO/RPO 演练；备份/PITR 由 11.10 独立验收，事务池兼容、只读副本评估及 11.6 的完整故障切换验收继续保持未勾选。

### 11.10 Phase 6 PostgreSQL 物理备份与时间点恢复验收记录（2026-07-21）

备份合同现区分每日 custom-format 逻辑 dump、带 SHA-256 manifest 的物理基础备份、连续 WAL 归档和目标时刻可恢复的 final Blob 快照。声明 PITR 时必须保留覆盖最老有效基础备份到批准恢复窗口的完整 WAL 链；恢复只能复制未修改基础备份到新的隔离数据目录，使用 `recovery.signal`、受控 `restore_command` 和唯一的时间/LSN/XID/name 目标。数据库到达目标并提升新 timeline 后，仍须配对对象快照并通过 schema、配额、ref_count、对象完整性和四范围分页对账才能切流。

`test_postgres_pitr_recovery.py` 在测试专属 PostgreSQL 18 源库开启连续 WAL 归档，加载真实 schema 与基线用户后创建 `pg_basebackup --wal-method=stream --manifest-checksums=SHA256`，原备份两次通过 `pg_verifybackup --exit-on-error`，而只修改 `PG_VERSION` 的独立副本被拒绝。夹具在基础备份后依次提交目标前和目标后业务行，确认包含目标后事务的 WAL 已归档并且 archiver 无失败，再停止源库；恢复只操作基础备份副本，不连接、不停止也不修改共享数据库。

隔离恢复以 inclusive LSN `0/3000750` 为目标，在 replay LSN `0/3000810` 完成并从 timeline 1 提升到 2；恢复库保留 schema、基线行和目标前行，排除位于 `0/30009D8` 的目标后行。4 个归档文件覆盖所需恢复范围，物理备份 manifest 的 WAL range 为 1，本机恢复耗时 0.109 秒。`0600` 原子证据 `.sisyphus/evidence/postgres-pitr-recovery-summary.json` 的 SHA-256 为 `a9eb1507e6901138222e7a2be9d2cc59200a391cae2c509f5ad1873fd3d0edf9`，不含密码、令牌、用户名、邮箱或连接串。

逻辑恢复/对象对账、物理 PITR、稳定端点晋升和拓扑合同聚焦 CTest 4/4 通过（20.24 秒）；完整 CTest 共 1388 项，1382 通过、6 项既有环境门控跳过、0 失败，总耗时 479.82 秒；OpenSpec 严格校验 24/24 通过。本记录结合既有 `BackupRestoreReconciliationIntegration` 关闭 11.2 的仓库级备份、时间点恢复与恢复演练流程，但本机 `cp` 归档不替代托管/pgBackRest/WAL-G、异地加密不可变存储、真实对象时间点快照或经批准的生产 RPO/RTO，因此 11.6 的完整依赖故障切换验收继续保持未勾选。

### 11.11 Phase 6 PostgreSQL 读副本准入评估记录（2026-07-21）

生产源码审计确认控制器、服务、Worker、健康检查和指标均只取得 Drogon 默认 `DbClient`。认证与用户状态、权限/分享、文件/目录/搜索/回收站、上传任务/分片/配额/租约、持久任务、管理员诊断与恢复、健康检查和指标都参与权威判断或没有最大陈旧时间合同；因此第一阶段读副本查询白名单为空，不能把普通 `SELECT` 当作安全准入依据。

`RuntimeConfig` 现在会在环境覆盖、数据库连接和服务初始化前要求 `db_clients` 精确包含一个名为 `default` 的对象；缺失、非数组、空数组、非对象、重命名或追加 replica 均拒绝启动，错误不回显配置中的客户端名称、主机或密码。`DistributedTopologyContract` 同时锁定默认/分布式模板各只有一个 `default` 客户端，并禁止生产源码取得命名数据库客户端。

clang-format、Python 语法检查、CMake 配置和完整构建通过；RuntimeConfig/拓扑聚焦 CTest 10/10 通过，完整 CTest 共 1391 项，1385 通过、6 项既有环境门控跳过、0 失败，总耗时 485.33 秒；OpenSpec 严格校验 24/24 通过。本记录完成 11.2 的“评估”要求但没有启用读副本；未来只有具有版本化陈旧读取合同、最大延迟、数据版本/观测时间、主库降级策略、隔离路由和故障测试的非权限报表可重新评估，所以第 17 节报表读副本演进项继续保持未勾选，事务池兼容项也不受本记录影响。

## 12. Phase 7：可观测性与运维工具

### 12.1 日志与追踪

- [ ] 所有结构化日志包含 `request_id`、`instance_id`；上传相关日志增加 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。
- [x] 禁止记录 JWT、share token、密码、S3 凭据和文件正文。
- [ ] 为 init/chunk/complete/download/cleanup 建立跨 API、DB、Worker、S3 的追踪关联。
- [x] 明确日志采样策略，避免大批量分片上传产生不可控日志量。

### 12.2 指标

- [x] 暴露请求量、错误率、延迟、活动上传数、分片吞吐和完成各阶段耗时。
- [x] 暴露上传状态数量、过期租约、接管次数、重试次数和失败终态数量。
- [x] 暴露任务队列深度、最老任务年龄、执行耗时、dead-letter 数量。
- [x] 暴露 PostgreSQL/Redis/S3 调用耗时、错误分类、连接池使用率和线程队列深度。
- [x] 暴露 staging/final 孤儿数量、缺失对象和配额/ref_count 对账差异。
- [x] 指标标签禁止使用 `upload_id`、文件名等高基数字段。

### 12.3 告警与运维命令

- [x] 为可用性、错误率、P99、任务积压、租约反复接管、dead-letter、S3 错误和对账差异设置告警。
- [x] 提供只读诊断命令：查看上传状态、租约、分片元数据、对象 HEAD 和关联任务。
- [x] 提供受审计的任务重放、解除死租约、重建清理任务和对账命令。
- [x] 运维命令默认 dry-run，破坏性操作要求精确 ID 和二次确认。

### 12.4 Phase 7 验收

- [x] 从一个失败请求可定位到具体实例、数据库状态、对象和恢复任务。
- [x] 故意制造任务积压、租约过期和 S3 失败时，对应指标与告警触发。
- [x] 运维人员无需直接修改数据库即可完成常见诊断和安全重试。

### 12.5 高频分片日志策略记录（2026-07-21）

生产 `INFO` 采用确定性的结果分层：成功分片的接收、参数、成功结果和 `[upload_chunk] outcome=success` 摘要固定为 `DEBUG`，常态保留比例为 0%；失败摘要及对应 `WARN`/`ERROR` 上下文不做概率采样，常态保留比例为 100%。请求数、字节数和延迟继续由低基数 Prometheus 指标无采样累计，操作审计不受高频诊断日志策略影响。临时 `DEBUG` 仅允许在从入口摘除的单个 API 上按最长 15 分钟和日志空间预算启用，且仍禁止记录凭据或文件正文。

`Logger::HighVolumeDetail`、`HighVolumeSuccess` 和 `HighVolumeFailure` 将策略集中为可复用接口，`FileController` 与 `UploadService` 的分片路径已改用该接口；`LogHelperTest` 通过内存 sink 锁定 `INFO` 丢弃细节/成功、保留失败，以及 `DEBUG` 恢复细节/成功的行为。日志策略聚焦 CTest 2/2 通过；CMake 配置和完整构建通过，完整 CTest 共 1383 项，1377 通过、6 项环境门控跳过、0 失败，总耗时 434.98 秒；OpenSpec 严格校验 24/24 通过。结构化字段与跨 API/DB/Worker/S3 追踪仍未全部收敛，因此 Phase 7 对应两项继续保持未勾选。

## 13. Phase 8：测试与验证

### 13.1 单元测试

- [x] 覆盖所有合法/非法上传状态迁移和版本号变化。
- [x] 覆盖租约认领、续租、过期接管和旧 owner 拒绝提交。
- [x] 覆盖分片幂等、终态拒绝、对象/DB 不一致处理。
- [x] 覆盖任务认领、去重、退避、dead-letter 和优雅关闭。
- [x] 覆盖 S3 错误分类、multipart abort、流式哈希和大对象 copy。
- [x] 覆盖重复完成、重复取消、配额与 ref_count 不变量。

### 13.2 多实例集成测试

- [x] 新增两个不同端口 API、两个 Worker、共享 PostgreSQL/Redis/MinIO 的 Compose 夹具和环境门控入口。
- [x] 多实例脚本固定 init 在 A、分片跨 A/B、complete 并发到 A/B，并验证另一实例可完成。
- [x] 多实例脚本同时向 A/B 上传同一分片，验证两次调用幂等并由完成后的 DB/对象唯一性兜底。
- [x] 多实例脚本同时向 A/B 调用 complete，冲突方重试后必须收敛到同一文件记录。
- [x] 并发执行 complete/cancel/expire，验证单一合法终态和配额不变量。
- [x] 多实例脚本在 A 登出/取消分享后从 B 立即验证 access/share token 撤销，并并发验证 refresh CAS。
- [x] 多实例脚本验证两个 Worker 共享队列，并在 A 停止后由 B 接管过期租约；周期任务去重继续由数据库集成测试覆盖。

`test_safety_upload_invariants.py` 对未过期和按 PostgreSQL 截止时间过期的任务分别从同一屏障并发发起 complete、cancel 与 expire，已在完整 CTest 中通过；它验证唯一合法终态、租约/分片清零、唯一 staging cleanup，以及 reserved/used、文件、内容行和 ref_count 不变量。`test_distributed_flow.py` 使用 API A 完成/过期扫描、API B 取消，并重复同一套数据库与 S3 对账；该 Compose/本地多实例入口仍受环境变量门控，本次只确认脚本合同存在，不把未执行的环境门控项计作目标环境验收。

### 13.3 故障注入

- [x] 分片对象写成功、DB 写失败后重试与孤儿清理。
- [x] DB 分片记录存在、对象被删除时完成失败与对账告警。
- [x] 完成认领后、组装前杀死实例。
- [x] 组装对象创建后、最终事务前杀死实例。
- [x] 最终事务提交后、HTTP 响应前杀死实例。
- [x] Blob 删除成功后、任务标记成功前杀死 Worker。
- [x] PostgreSQL/Redis/S3 分别短暂不可用后恢复。
- [x] 租约续租超时、网络分区和旧 owner 恢复后尝试提交。

### 13.4 数据与迁移测试

- [x] 使用当前 schema 和代表性数据执行完整升级，验证用户、文件、上传和配额不变。
- [x] 在 expand 阶段混跑新旧二进制，验证兼容性。
- [x] 验证 local 暂存进行中任务在迁移窗口内的处理策略。
- [x] 如迁移存量 Blob，逐对象校验大小和哈希，并测试中断续跑与重复执行。
- [x] 执行备份恢复后，再运行配额、ref_count、DB/对象一致性对账。

### 13.5 压力与容量测试

- [x] 分别测量 1/2/N API 实例的扩展效率，确认瓶颈位于预期组件。
- [x] 覆盖小文件、高并发分片、大文件完成、Range 下载和混合读写负载。
- [x] 测量 S3 流式组装带宽、Worker CPU、网络、内存和临时对象增长。
- [x] 在一个 API/Worker 故障时继续施压，验证错误预算和恢复时间。
- [x] 测量任务积压恢复速度，避免恢复流量压垮在线请求。
- [x] 根据结果固定每实例连接池、线程池、并发和副本建议值。

### 13.6 必跑验证命令

- [x] `cmake --preset linux-debug-clang`
- [x] `cmake --build --preset linux-debug-clang`
- [x] `ctest --preset linux-debug-clang -V`
- [x] 新增并运行多实例集成测试入口。
- [x] 新增并运行 S3/MinIO 环境门控测试。
- [x] 运行更新后的压力测试并保存基线对比证据。

## 14. Phase 9：迁移、灰度与回滚

### 14.1 上线前准备

- [x] 完成数据库和对象存储备份，验证恢复可用。
- [x] 创建 staging/final bucket 前缀、生命周期、权限和监控。
- [x] 发布 expand schema，确认旧代码继续正常运行。
- [x] 发布同时支持旧任务和新任务的过渡版本，但默认仍创建旧模式任务。
- [x] 部署 Worker，先以不认领或 dry-run 模式验证查询与指标。

2026-07-20 已完成仓库门禁和本机隔离预发布演练：真实 Worker 以
`worker_claiming_enabled=false` 启动并保持 readiness，通过数据库查询暴露队列指标；跨越 6 个配置
轮询周期后，预置 Ready 任务仍为 Pending、尝试次数为 0、无 owner/lease，且未播种周期任务。
目标预发布/生产环境必须按部署指南 4.5.1 使用真实依赖和部署身份重复该门禁，本记录不宣称已完成生产发布。

### 14.2 上传暂存切换

- [x] 为新任务启用 S3 staging feature flag，并在 `upload_tasks.staging_backend` 固化选择。

2026-07-20 已将分布式 Compose 的 `DISK_UPLOAD_STAGING_BACKEND` 暴露为可覆盖的启动期开关，
最终拓扑默认 `s3`。候选提交 `ff2ea6b` 的本机隔离门禁使用唯一临时 PostgreSQL 数据库
和 Moto 5.2.2，4.443 秒内完成 1 次 6 MiB/6 分片 S3-native 上传；对账显示 1/1 任务固化
`staging_backend=s3`，完成组装、final 提升和 Worker 清理，API/Worker 本地暂存均为 0。
目标预发布/生产环境仍必须按部署指南 4.5.2 使用真实端点和部署身份重复该门禁。

- [x] 小比例用户/实例灰度，监控成功率、完成耗时、对象错误、租约和对账差异。

2026-07-20 已完成候选应用基线 `d5c7d24` 的仓库级 10% 隔离灰度：两个 API
分别以 local/S3 为新任务默认值，9 个 baseline 和 1 个 canary 任务均为 6 MiB/6 分片，
精确固化描述符并全部完成。baseline 成功率 100%、完成均值 249.394 ms、P99 267.136 ms；
canary 跨实例完成且成功率 100%，完成耗时 425.911 ms，S3 成功调用 86 次且
可操作错误为 0，过期租约、接管、死信和未解决 finding 均为 0，10/10 cleanup 收敛。
完整 CTest 为 1371 通过、6 项环境门控跳过、0 失败；结构化证据为
`.sisyphus/evidence/staging-canary-summary.json`。目标预发布/生产环境仍须使用真实端点和部署身份复测。

- [x] 逐步扩大流量，不修改已创建任务的 staging backend。

2026-07-20 已完成候选应用基线 `f3ebd79` 的仓库级 10% → 50% → 100%
扩流门禁：三阶段新任务的 local:S3 比例精确为 9:1、5:5、0:10；30 个任务经过
5 个检查点后 backend/prefix 变更数为 0。切换前已写首分片的 local/S3 哨兵任务
在所有 API 改为 S3 默认后均完成，30/30 cleanup 成功，reserved、可操作 S3 错误和
未解决 finding 均为 0。完整 CTest 为 1372 通过、6 项环境门控跳过、0 失败；
结构化证据为 `.sisyphus/evidence/staging-rollout-expansion-summary.json`。目标环境仍须按
部署指南执行含 25% 阶段的完整观测窗口。

- [x] 旧 local 任务通过原节点完成、自然过期或维护窗口取消；禁止假装它们可被任意节点恢复。

2026-07-20 已在候选应用基线 `465461d` 上完成旧 local 暂存排空门禁。固定 V002
在原卷创建三个进行中任务；迁移后，原卷/错误卷 current API 对同一分片分别只读诊断为
`present`/`missing`，且任务、分片、租约、配额和 job 快照不变。随后仅由挂载原卷的
current API/唯一 Worker 将任务分别收敛为 Completed、Cancelled、Expired，3/3 cleanup
成功，local 非终态、未完成 cleanup、reserved、原卷暂存文件和错误节点文件均为 0。
`LocalStagingMigrationIntegration` 通过（完整套件内 4.40 秒）；完整 CTest 为 1372 通过、
6 项环境门控跳过、0 失败，总耗时 357.71 秒。结构化证据为
`.sisyphus/evidence/local-staging-drain-summary.json`，SHA-256 为
`8ee371a2b9311c5b225557d5194c21b9348f1238dfdfbe410b088b7cf9534a09`。目标环境仍须按节点生成
真实归属清单并保存逐卷扫描结果，仓库门禁不代替生产排空。

- [x] 旧任务归零后禁用生产 local staging 创建路径。

2026-07-21 已在候选应用基线 `8fe724a` 上封死生产 local staging 创建路径。
`DISK_SECURE_MODE=true` 的显式 API 若选择 local staging，会在存储/外部依赖初始化和监听前以
退出码 1 失败；显式 Worker 仍可保留 local 适配器完成遗留清理，非安全模式 API/local 仅保留
开发测试兼容。真实二进制门禁确认预期错误出现、随机端口始终未开放且未超时；配置与启动聚焦
CTest 38/38 通过。完整 CTest 共 1380 项：1374 通过、6 项环境门控跳过、0 失败，总耗时
371.90 秒；OpenSpec 严格校验 24/24 通过。结构化证据为
`.sisyphus/evidence/local-staging-cutoff-summary.json`，SHA-256 为
`5aa86b02cab5166ace8c46b8798cc1292c881822f78ee27d5dce0958b0f08523`。目标环境仍须先保存
local 非终态、未完成 cleanup 与逐原卷扫描全部归零的证据，再发布本门禁制品。

### 14.3 多实例与 Worker 切换

- [x] 先启用两个 API 实例但保持可快速摘除新实例。

2026-07-21 已在候选应用基线 `d1702d5` 上完成仓库级双 API 可逆准入门禁。基础 Compose 明确
提供 `api-a`/`api-b` 及 A/B upstream，`deploy/docker-compose.api-a-only.yml` 只在相同挂载目标
替换为仅 A upstream；`scripts/switch-api-pool.sh` 在双池与仅 A 模式下都先执行完整 Compose 校验，
成功后才只重建 load-balancer，不负责停止 API。fake Docker 验证两种覆盖选择、校验先于 apply、
校验失败不 apply 及非法模式不调用 Docker；与既有拓扑合同的聚焦 CTest 2/2 通过。完整 CTest
共 1381 项：1375 通过、6 项环境门控跳过、0 失败，总耗时 360.82 秒；OpenSpec 严格校验
24/24 通过。结构化证据为 `.sisyphus/evidence/api-pool-rollout-summary.json`，SHA-256 为
`4652598ce305c52577932acade0124d127e62bfde0fc70df02407f51637f4913`。本机未安装 Docker/Nginx，
目标环境仍须按部署指南验证 B 直连 readiness 与实例响应头、加入双池、先切回仅 A 并连续确认
入口只返回 A 后再终止 B；本项不替代下一条无 sticky 的真实随机路由验收。

- [x] 关闭 sticky session，运行真实随机路由验收。

2026-07-21 已在候选应用基线 `1191889` 上完成仓库级真实随机路由门禁。Nginx 配置继续由静态合同
确认不存在 cookie/ip-hash/sticky，测试专用本地入口改为随机洗牌池且不重放非幂等请求；
`DistributedLocalFlowIntegration` 以隔离 PostgreSQL、Redis、固定 MinIO、两个真实 API/Worker 和
随机代理执行 1/1 通过，共 17 项检查、耗时 132.72 秒。同一持久 HTTP 客户端携带固定 cookie，
只访问入口完成一个上传，init/chunk/complete/download 的实例序列为
`disk-api-b -> disk-api-a -> disk-api-b -> disk-api-b`；同分片一次重试即覆盖 A/B，最终任务、
文件/内容引用、配额、S3 final 对象和下载字节全部一致。证据为
`.sisyphus/evidence/distributed-flow-summary.json`，SHA-256 为
`f00fb45892199fce2cacefaae7ef0953eda0b714da4fb922c9679be66a6bc2a8`。完整 CTest 共 1381 项：
1375 通过、6 项常规环境门控跳过、0 失败，总耗时 361.71 秒；OpenSpec 严格校验 24/24 通过。
受管进程和临时拓扑已清理。本机没有 Docker/Nginx，目标环境仍须用真实 Nginx `least_conn` 入口
复跑同一响应头与数据对账；本地门禁证明应用不依赖节点亲和，不替代目标网关加载确认。

- [x] 启用 Worker 任务执行后，关闭 API 中的集群级 `ScheduledTasks`。

2026-07-21 已在候选应用基线 `dd41910` 上完成 Worker/API 调度所有权切换门禁。
`SchedulerRoleCutoverIntegration` 以唯一临时 PostgreSQL、隔离 local storage 和三个真实后端
进程按 API A -> Worker -> API B 的顺序执行：API A/B 都显式继承
`worker_claiming_enabled=true`，但 readiness 的有效认领值均为 false，两者日志的 seeder
记录均为 0；只有 Worker 启动一个 seeder，并将当前 UTC 窗口中 1 个
`expire_uploads`、1 个 `expire_trash` 和 4 个 `storage_reconcile` 首页任务各执行
一次并收敛为 Succeeded。API B 加入和摘除后，包含 payload、状态、尝试次数、
租约与时间戳的完整六行快照与基线逐字段相等。聚焦回归 13/13 通过；完整
CTest 共 1382 项：1376 通过、6 项常规环境门控跳过、0 失败，总耗时 362.12 秒；
OpenSpec 严格校验 24/24 通过。结构化证据为
`.sisyphus/evidence/scheduler-role-cutover-summary.json`，SHA-256 为
`960d00f7675d75ef59f687d2c8ba751b24762249c4b2bb0d245154e71995f93f`。本机门禁证明应用角色
分离与数据库不变式；目标预发布/生产环境仍须按部署指南用实际编排重复 Worker
重启、API B 加入/摘除和完整行快照比对。
- [x] 逐步增加 API/Worker 副本并重新核算 PostgreSQL、Redis、S3 连接和并发预算。

2026-07-21 已在候选应用基线 `5fdb94b` 上完成仓库级逐级容量门禁。既有重构后基线已经真实
测量 1/2/4 API 与 1/2/4 Worker；新增 `scripts/check-distributed-capacity.py` 从最终分布式配置
读取每进程池与并发值，逐项核算 `2+2`、`2+4`、`4+2`、`4+4`。四档应用聚合预算依次为
PostgreSQL `32/48/48/64`、Redis `16/24/24/32`、S3 HTTP `64/96/96/128`、S3 I/O
`16/24/24/32`；最大档加一个滚动替换进程后分别需要 `72/36/144/36`，在本次显式审阅的
`100/36/144/36` 配额内通过。合同测试同时证明四类预算各差 1、API/Worker 超过已测 4 副本、
重复/畸形计划及每进程配置漂移都会非零退出，失败证据标记 `acceptance.passed=false`，且 JSON
不包含密码或 endpoint。聚焦 CTest 2/2 通过；完整 CTest 共 1383 项：1377 通过、6 项常规环境
门控跳过、0 失败，总耗时 367.68 秒；OpenSpec 严格校验 24/24 通过。结构化证据为
`.sisyphus/evidence/distributed-capacity-plan.json`，SHA-256 为
`360e31b8d3a8c99f286d229c84974c86293c9d56c9c02716fc5ebd60465a9ba9`。Redis/S3 数值是
仓库样例的最小应用侧配额，不代表目标托管服务能力；预发布/生产仍须输入实际审批上限，并在每档
扩容后观察 readiness、错误率、P99、连接利用率、S3 限流与 Worker 积压。
- [x] 观察至少一个完整上传过期/回收周期后再执行 contract 清理。

2026-07-21 已在候选应用基线 `6783085` 上完成仓库级 data-to-contract 观察门禁。新增
`ContractReadinessCycleIntegration` 以真实 S3-only API 创建并写入探针，不改数据库生成的
`expires_at`；配置/持久 TTL 均为 3 秒，数据库与墙钟分别自然经过 3.098/3.741 秒后才启动 claiming
Worker。Worker 首次小时级播种与唯一 S3 cleanup 均一次成功，探针收敛为 Expired，reserved、chunk、
staging 对象全部归零；随后新上传完成并逐字节下载，文件/内容/ref_count 唯一，used/reserved 为
4992/0。新的 `contents/users/staging/final` 对账各一页、一次成功，全部 contract 阻断计数为 0，
API/Worker seeder 数为 0/1，且证据不含凭据、endpoint、upload ID 或对象 key。聚焦 CTest 1/1
通过（7.65 秒）；完整 CTest 共 1384 项：1378 通过、6 项常规环境门控跳过、0 失败，总耗时
373.28 秒；OpenSpec 严格校验 24/24 通过。证据为
`.sisyphus/evidence/contract-readiness-cycle-summary.json`，SHA-256 为
`8c59aa62d5e7fe6f93861e864e3ec473cb5b3b25a137aadc99d8554c7eb22e8c`。该结果只准入 V005 设计评审，
未执行 DDL；预发布/生产仍须等待实际 TTL 加后续独立小时扫描并保存自身全量归零证据。

### 14.4 存量 final Blob 迁移（仅现网使用 local 时）

- [x] 先生成只读迁移清单：content ID、旧路径、大小、MD5、SHA-256、目标 key。

2026-07-21 已在候选应用基线 `14e2473` 上完成只读 final Blob manifest 仓库门禁。清单从
PostgreSQL `REPEATABLE READ READ ONLY` 完整快照生成，包含 `ref_count=0` 行并按 content ID 排序；
每行精确包含 content ID、原 locator、local-root 相对路径、大小、MD5、SHA-256 和规范目标 key。
manifest 阶段在移除 S3 凭据的环境中运行，证明 DB 与源 Blob 快照不变、S3 请求为 0、
不创建 checkpoint；输出以 `0600` 权限落盘，并使用原子不覆盖发布保护启动前已有或生成期间并发出现的同名证据。
末行校验故障和发布冲突均非零退出且临时/最终半成品为 0。

`FinalBlobMigrationIntegration` 聚焦 CTest 1/1 通过（7.80 秒）；完整 CTest 共 1384 项：
1378 通过、6 项常规环境门控跳过、0 失败，总耗时 371.91 秒；OpenSpec 严格校验 24/24 通过。
去敏证据为 `.sisyphus/evidence/final-blob-manifest-summary.json`，SHA-256 为
`6b0f8b4c6480c6bfae42ff82b7bb54968b722aebc7e494433af1d32f73e3b734`。该证据不是现网 inventory；目标环境仍须在
停写与 PostgreSQL/local Blob 备份完成后生成自身全量 manifest、记录其 SHA-256 并完成人工评审。

- [x] 迁移工具支持 checkpoint、限速、重复执行、单对象校验和 dry-run。

2026-07-21 已在候选应用基线 `9211c88` 上完成 final Blob 可恢复复制仓库门禁。三对象 dry-run
完整检查源和目标但 PUT/checkpoint/DB 变化均为 0；单对象执行批次以 0.01 MiB/s 聚合预算处理
3807 字节对象，上传加完整 GET 计费 7614 字节，预期有界墙钟下限 0.726 秒且实测通过，
仅提交一条 `0600` checkpoint。真实 `SIGKILL` 发生在第二对象 PUT 期间，checkpoint 只含首个已完整验证对象；
恢复后重验 1 个、补传 2 个。checkpoint 绑定 manifest SHA-256/bucket/object prefix，篡改绑定在 PUT 前拒绝；
恢复后全量重放重验 3 个目标且新 PUT 为 0。损坏源、冲突目标和同大小目标哈希损坏均非零拒绝。

`FinalBlobMigrationIntegration` 聚焦 CTest 1/1 通过（9.33 秒）；完整 CTest 共 1384 项：
1378 通过、6 项常规环境门控跳过、0 失败，总耗时 372.70 秒；OpenSpec 严格校验 24/24 通过。
去敏证据为 `.sisyphus/evidence/final-blob-copy-summary.json`，SHA-256 为
`211b21a25a76897f3f02ee406f48f4f67f84b8de030e7723aff2523b7b2e4383`。目标环境仍须对已评审 manifest 先执行
dry-run，根据维护窗口和 S3 容量确定实际批次/限速，保存 checkpoint 并在切换前完成自身全量校验。

- [x] 选择维护窗口切换，或设计有截止日期的 per-content backend/双读迁移；禁止无边界长期双写。

2026-07-21 已在候选应用基线 `cdae48f` 上固定存量 final Blob 迁移策略：不增加 per-content
backend，选择最长 120 分钟且不得延期的停写维护窗口。`deploy/final-blob-maintenance-window.json`
以 18 个有序门禁固定关闭入口、停 API/Worker/定时任务/Blob GC、备份、manifest、复制与 checkpoint
验证、原子 cutover、S3 配置、下载/Range 探针、全量对账和开放流量的顺序；要求迁移、数据库、
回滚三类负责人和 10 类停止条件，明确禁止在线部分切库、在线双写与无截止日期双读。窗口在 cutover
前超时时保留 checkpoint 并只能在新审批窗口续传；cutover 后、开流量前超时或验证失败时，必须保持入口关闭并
恢复同一 manifest 中的全部原路径。

`scripts/check-final-blob-cutover-plan.py` 对受评审策略执行严格 schema/字段/顺序检查，以 `0600`
原子写入确定性去敏证据。合同测试证明策略改为 per-content、允许延期、取消双写禁令、乱序/缺失门禁、
缺回滚负责人、缺超时停止条件、切库后继续开流量、不保留 local 源、非法 JSON 或混入 endpoint 都会非零拒绝，
且证据不泄漏 endpoint 值。聚焦 CTest 1/1 通过（1.06 秒）；完整 CTest 共 1385 项：1379 通过、
6 项常规环境门控跳过、0 失败，总耗时 376.26 秒；OpenSpec 严格校验 24/24 通过。去敏证据为
`.sisyphus/evidence/final-blob-cutover-plan.json`，SHA-256 为
`33d2954eb82f902c502526c5e5b3d9ee60cb4ad36b03d8c2a5227916040febb2`；其中策略文件 SHA-256 为
`125da5f9d380fd564364c5a26b21a24947c22fd6c8626b43a6179396012a8458`。该证据不是生产变更批准；目标环境仍须在变更单中
填写实际 UTC 起止时间和三类值班人，在窗口前重跑门禁并保存自身审批/执行证据。

- [x] 所有对象复制并验证后再切换数据库读取位置。

2026-07-21 已在候选应用基线 `8f6fc8d` 上完成 final Blob 全量验证后原子 cutover 仓库门禁。
cutover 以只读方式打开与 manifest SHA-256、bucket 和 object prefix 精确绑定的 checkpoint，先要求
每个 manifest 对象存在完整验证记录，再逐个完整 GET 目标并重算大小、MD5 与 SHA-256；只有全部通过才进入
PostgreSQL `SERIALIZABLE` + advisory lock + `ACCESS EXCLUSIVE NOWAIT` 事务。事务以 manifest 临时表对比
`file_contents` 完整集合，只接受全部 source 或全部 target 状态；前者一次更新全部路径，后者幂等更新 0 行，
新增、删除、元数据漂移或混合路径均整体拒绝。cutover 本身 PUT 为 0，不在切库阶段补传或修复对象。

`FinalBlobMigrationIntegration` 以三对象证明：无 checkpoint 时切换在 S3 请求前拒绝；进程于第二对象 PUT
期间被 `SIGKILL` 后 checkpoint 仅有 1/3，cutover 只重验已记录的一个目标就在缺失记录处停止；
全量复制后将目标替换为同大小错误字节仍被双哈希拒绝。三类失败均保持全部 source locator 不变。
成功执行在切换前完整 GET 3/3 且 PUT 0，单事务更新 3 行后数据库全部指向规范 S3 key；重放更新 0 行。

聚焦 CTest 1/1 通过（10.33 秒）；完整 CTest 共 1385 项：1379 通过、6 项常规环境门控跳过、0 失败，
总耗时 378.53 秒；OpenSpec 严格校验 24/24 通过。去敏证据为
`.sisyphus/evidence/final-blob-cutover-summary.json`，SHA-256 为
`de3e61792ca549605df50a34aaeed714769a50677d1075568489337377041d81`。原 manifest/copy 证据哈希保持不变。
该 fixture 证明工具安全属性，不代表现网已完成复制或切换；目标环境必须在停写窗口使用自身 manifest/checkpoint
先运行 cutover dry-run，保存全量目标重验与 DB 快照证据，再经双人审批执行一次原子切换。

- [x] 完成全量对账和备份后，才安排旧本地 Blob 回收。

2026-07-21 已在候选应用基线 `5b0ec44` 上完成旧 local Blob 延迟回收的仓库级排期门禁。
`deploy/final-blob-maintenance-window.json` 的 `retirement` 合同固定为 `schedule_only`，不包含破坏性动作，
要求真正删除另走独立审批。排期只能在 cutover 证据验收、回滚窗口关闭、切换后一致恢复集及 manifest
哈希完成、空隔离环境恢复演练、下载/Range 探针、`contents/users/staging/final` 四 scope 全分页对账、
未完成任务/finding/配额/ref_count 阻断计数归零、source inventory 与原 manifest 一致以及
manifest/checkpoint 归档之后准入；恢复集必须持续保留，最早回收日期为全部 18 个有序门禁通过后 30 天，
并要求存储、备份和回滚负责人审批。

`FinalBlobCutoverPlanContract` 逐项删除 18 个前置门，并验证提前回收、`execute_delete` 模式、夹带破坏性动作、
取消独立审批、缺备份负责人和提前审批均非零拒绝；AST 命令面检查同时固定
`migrate-final-blobs.py` 只有 `manifest/copy/cutover/rollback`，没有 delete/retire 子命令。准入证据以
`0600` 原子发布且不读取或修改 local 源。聚焦 CTest 1/1 通过（2.48 秒）；完整 CTest 共 1385 项：
1379 通过、6 项常规环境门控跳过、0 失败，总耗时 385.13 秒；OpenSpec 严格校验 24/24 通过。
去敏证据为 `.sisyphus/evidence/final-blob-cutover-plan.json`，SHA-256 为
`799f082f31193459dcf24d5df17b99bce61baf27340eacfd8f3603e16c38ec76`；受评审策略文件 SHA-256 为
`8e7b42b5c6291fe50784edeb3da6b793977f3b8bd553e070770f13938f529069`。
该结果只准入目标环境变更单中的延迟排期，不代表现网已完成备份、对账、等待期或删除；实际隔离/删除前
仍须以目标环境当时的 manifest 重验 source inventory、恢复集保留状态和 S3 全量对账，并单独审批。

### 14.5 回滚

- [x] feature flag 可以停止创建新 S3 staging 任务，但不能把已创建的新任务交给不理解新 schema 的旧版本。

2026-07-21 已在候选应用基线 `99388b7` 上完成新任务创建截止门禁：
`upload_task_creation_enabled` / `DISK_UPLOAD_TASK_CREATION_ENABLED` 是启动期严格布尔开关；
false 只在秒传和 resume 解析之后、配额预留与新任务插入之前返回 `503/50012`。
门禁证明全新 init 拒绝前后任务数、新 hash 任务数、已用/预留配额与
`users.xmin` 不变；已存 S3 任务返回原 `upload_id` 和分片 `[0]`，local/S3 哨兵
均可完成，秒传仍可用，30/30 cleanup 收敛。6 个描述符检查点的变更数为 0。

旧二进制不理解该开关，所以运维合同明确要求其不得进入 S3/`Finalizing` 的 init、
chunk、complete、cancel 或 Worker 路由；仓库门禁只使用兼容当前版本，不伪造旧版本运行证据。
完整 CTest 共 1386 项：1380 通过、6 项环境门控跳过、0 失败，总耗时 380.90 秒；
OpenSpec 严格校验 24/24 通过。结构化证据为
`.sisyphus/evidence/staging-rollout-expansion-summary.json`（SHA-256
`3df62342dfa5af3e6305d1593ac6642231688cf9bcd80c21e4a4ecf62692ec0e`）。

- [x] 回滚应用前先停止新上传并处理/冻结新状态任务，避免旧版本误读 `Finalizing`。

2026-07-21 已在候选应用基线 `897fb50` 上完成上传生命周期 drain/freeze 回滚门禁。
兼容 API readiness 新增启动期固定的 `upload_task_creation_enabled` 与实时
`business_requests_inflight`；受评审 Nginx freeze 片段在应用和鉴权之前拦截
`/api/file/upload` 及全部子路径，固定返回 `503/50013`、`Retry-After: 30` 和
`Cache-Control: no-store`。重开入口必须显式设置 `DISK_UPLOAD_UNFREEZE_APPROVED=true`。

唯一临时 PostgreSQL 夹具包含 1 个 `InProgress` 和 1 个持有有效租约的 `Finalizing` S3
任务：freeze 模式在只读重复读快照中保留两行、租约、版本和 staging 描述符且 `xmin` 不变；
drain 模式先稳定拒绝，只有两行进入终态后才以活动任务数 0 通过。门禁还分别拒绝在途业务请求、
新任务创建未关闭和上传入口未冻结，并始终输出 `old_release_upload_route_allowed=false`。
完整 CTest 共 1389 项：1383 通过、6 项环境门控跳过、0 失败，总耗时 383.43 秒；
OpenSpec 严格校验 24/24 通过。结构化证据为
`.sisyphus/evidence/upload-rollback-gate-summary.json`（SHA-256
`08082a1ccc00cb952c005b8fe33b302e999ab17f7ceba31bc79bff923b191a03`）。该仓库门禁只证明
上传处理面可被排空或冻结，不准入旧版本访问其他新 schema/Blob；目标环境仍须保存自身入口、
直连兼容实例和数据库快照证据。

- [x] expand schema 默认保留；数据库 contract 迁移不得作为紧急回滚步骤。

2026-07-21 已在候选应用基线 `e403ce2` 上完成 expand schema 保留门禁。紧急应用回滚证据固定为
`schema_action=preserve_expand`、`contract_migration_allowed=false`；前向 manifest 不含
`DROP TABLE/COLUMN/DATABASE`。V002/V003/V004 三份破坏性 rollback SQL 均在事务内、首条 DDL 前
复核预激活上下文、独立批准、变更单与 readiness SHA-256；无参数直接执行 3/3、紧急上下文 1/1、
逐项缺少批准/变更单/摘要 3/3 均失败，schema 与迁移账本变更数为 0。白名单入口的未知版本、紧急
上下文、未批准、非法变更单和非法摘要共 5/5 在调用 `psql` 前拒绝；V004 非空数据门禁继续拒绝，
空表经批准的预激活撤销及随后前向恢复成功。聚焦 CTest 6/6 通过（13.21 秒）；完整 CTest 共
1390 项：1384 通过、6 项环境门控跳过、0 失败，总耗时 394.13 秒；OpenSpec 严格校验 24/24
通过。`0600` 结构化证据为 `.sisyphus/evidence/schema-reversal-guard-summary.json`（SHA-256
`dfbb9ff82c1073ac499751ae424c9f0da13ba3bcd36ee053b9c28256630c2691`）。该隔离库门禁未执行生产
contract DDL；未来 contract 必须使用新的 DDL、恢复演练、负责人、目标环境证据和独立审批。

- [x] Worker 可停止认领，已持有任务依靠租约到期恢复。

2026-07-21 已在候选应用基线 `2db29eb` 上完成 Worker 逐台回滚与自然租约接管门禁。分布式
Compose 为所有应用实例固定 `stop_grace_period=${DISK_STOP_GRACE_PERIOD:-40s}`，严格覆盖生产配置的
30 秒应用 drain；拓扑合同同时防止两者漂移。`WorkerDrainTakeoverIntegration` 使用唯一临时 PostgreSQL、
两个真实 Worker 和外部行锁阻塞 A 已认领的 Blob GC：SIGTERM 后 A 的 readiness 为 503，报告
`draining=true`、认领能力仍启用但接受值为 0，且没有开始新任务或新 seed cycle；测试专用 2 秒 drain
截止点后 A 以 0 退出，目标行仍为 A 持有的 `Running/attempts=1` 且原租约有效，手工任务更新为 0。
B 先以 attempts=1 完成信号后创建的独立哨兵，只在目标的持久租约到期后以 attempts=2 正常接管并完成，
最终内容行和 Blob 均为 0。

聚焦 CTest 32/32 通过（63.66 秒）；完整 CTest 共 1391 项：1385 通过、6 项环境门控跳过、0 失败，
总耗时 416.23 秒；OpenSpec 严格校验 24/24 通过。`0600` 结构化证据为
`.sisyphus/evidence/worker-drain-takeover-summary.json`（SHA-256
`9e4a6f9d587e1557f13876d2bbe8711931cb0881b84a1e2d2111910bc44c0fe9`）。该本机隔离演练证明应用、
数据库与 Compose 合同，不替代目标编排器逐台终止、实际宽限时间和目标数据库只读快照证据。

- [x] S3/DB 已成功但响应失败的任务通过幂等 complete 恢复，不手工删除对象。

2026-07-21 已在候选应用基线 `c5622d1` 上完成 S3/DB complete 响应丢失恢复门禁。
`CompleteResponseLossIntegration` 使用唯一临时 PostgreSQL、开启版本控制的 Moto bucket、
两个真实 API 和一个真实 Worker：API A 在 final S3 对象提升且数据库终态事务提交后、
HTTP 响应前被 SIGKILL，客户端未收到成功响应；API B 使用同一用户和 `upload_id` 立即重放
complete，以 200 返回首次已提交的同一 `file_id`，不等待新租约。重放前后任务始终为
`Completed/attempts=1`且完整快照不变，文件、内容、ref_count、配额结算和 cleanup 任务均唯一。
final 对象仅 1 个版本、0 个 Delete Marker，版本 ID 和字节内容在重放后不变；Worker 以
attempts=1 清理 staging 并收敛到 0 个当前 staging 对象。夹具对业务表直接更新和 final
对象手工删除调用均为 0。

聚焦 CTest 75/75 通过（122.76 秒）；完整 CTest 共 1392 项：1386 通过、6 项环境门控跳过、
0 失败，总耗时 425.88 秒；OpenSpec 严格校验 24/24 通过。`0600` 结构化证据为
`.sisyphus/evidence/complete-response-loss-summary.json`（SHA-256
`3c26b4e145a97e74f920ad1970b7e93a8a6a544806aee23f5e81abc6aca468fa`）。该本机 Moto 隔离门禁不替代
目标 MinIO/云 S3 和编排器演练；目标环境仍须保存自身的首次连接失败、重放响应、数据库
只读快照、cleanup 终态及 final 版本快照。

- [x] 每个灰度阶段都有明确停止条件、回滚负责人、操作命令和数据校验查询。

2026-07-21 已在候选应用基线 `d24cd14` 上固化 S3 staging 逐阶段执行契约。
`deploy/staging-rollout-plan.json` 固定 `0 -> 10% -> 25% -> 50% -> 100%` 相邻扩流和反向回退，
四档最小观测窗口为 30/30/60/120 分钟且每档至少 20 个非秒传任务；每档完整绑定 14 条停止条件、
`release_owner`/`rollback_owner`/`database_verifier`/`storage_verifier` 四类实际责任人、精确
preview/apply/rollback 命令及 7 组数据查询。`scripts/staging-rollout.py` 在调用目标发布适配器前拒绝
缺 25%/缺责任人/缺条件或查询、跳档、占位责任人、相对适配器、未批准执行和含写操作 SQL；适配器仅接收
分离参数，不继承 PostgreSQL、Redis、S3 或 JWT 凭据，非零退出码原样传播。

`deploy/staging-rollout-validation.sql` 已对本机当前 `disk` schema 在单个 `REPEATABLE READ READ ONLY`
事务中执行成功。聚焦 CTest 4/4 通过（23.57 秒）；完整 CTest 共 1393 项：1387 通过、6 项环境门控
跳过、0 失败，总耗时 420.91 秒；OpenSpec 严格校验 24/24 通过。`0600` 结构化证据为
`.sisyphus/evidence/staging-rollout-plan-summary.json`（SHA-256
`f5a66c63570abca2daa01232aebdaacf4d54108e87aee45026b20726711005ce`）；受评审计划和 SQL 的
SHA-256 分别为 `8380abf9d97188bfda1451cf2179f70fecdd62194549c5e24a85b46bd546156d` 和
`ad3750da8537ccfdbe1fe73584e2727d423cfcd15fe5a2efd75e2e8eae931b09`。fake 发布适配器和本机数据库
只证明仓库合同；目标环境仍须填入真实值班人和变更单，由实际适配器校验当前档位，并完成全部观测窗口、
真实数据快照及回退演练。

### 14.6 Phase 9 验收

- [ ] 灰度和回滚至少在预发布环境完整演练一次。
- [ ] 上线期间无配额、ref_count、文件记录或 Blob 丢失。
- [ ] 所有遗留 local 上传和迁移兼容状态均可查询并有截止日期。

## 15. Phase 10：收尾与技术债清理

- [ ] 删除生产路径对 sticky session 和本地上传暂存的依赖。
- [x] 删除被数据库租约替代的跨请求 `AssemblyWorkerPool` 单飞职责；仅保留仍有明确局部价值的并发控制。
- [ ] 删除过渡 feature flag、旧 schema 字段、旧配置和旧迁移分支。
- [x] 删除已被 Worker 替代的 API 集群级定时任务注册。
- [x] 更新所有架构图、部署样例、运维手册、错误码表和测试数量。
- [ ] 运行 clang-format、完整构建、完整测试、多实例测试和压力测试。
- [x] 完成安全审查：S3 key 注入、SSRF/endpoint 配置、凭据泄漏、跨用户 upload ID、重放与限流。
- [x] 完成数据一致性审计：用户配额、文件数、内容 ref_count、DB/对象存在性、staging/multipart 孤儿。
- [x] 记录最终容量建议、已知限制和下一次扩容触发条件。

### 15.1 本机组装限流职责收敛记录（2026-07-21）

旧 `AssemblyWorkerPool` 及其 `m_active_upload_ids`、`IsUploadActive`、带 `upload_id` 的获取/释放接口已删除，替换为无任务标识的 `AssemblyConcurrencyLimiter`。Local/S3 组装准入只在本机容量耗尽时返回稳定的 `429 + 10005`；同一上传的有效租约冲突继续由 `UploadTaskRepository::ClaimFinalizeLease` 在组装前返回 `409 + 10004`。重复 DTO/模拟单飞测试已删除，限流器测试直接覆盖真实实现的 RAII、移动、容量恢复和并发上限；真实 PostgreSQL 完成集成中 6 个并发请求得到 1 个成功与 5 个租约冲突。

聚焦存储/状态机 CTest 79/79 通过，完成租约与组装背压集成 2/2 通过；完整构建成功，完整 CTest 共 1377 项：1371 通过、6 项既有环境门控跳过、0 失败，总耗时 425.36 秒；OpenSpec 严格校验 24/24 通过。环境门控的 S3/分布式拓扑测试仍不计作目标环境多实例验收，不据此勾选 Phase 9 或最终 Definition of Done。

### 15.2 分布式存储安全审查记录（2026-07-21）

S3 对象键边界已覆盖会话标识、staging 前缀、assembled key 和 final key：所有派生键在发起 S3 调用前完成校验，拒绝斜杠、反斜杠、编码分隔符、控制字符和路径穿越输入；清理只删除精确会话前缀，不影响相邻会话。S3 endpoint 仅接受启动时受信配置中的纯 origin，要求 `http://`/`https://` 与 TLS 开关严格一致，支持 DNS、IPv4、方括号 IPv6 和合法端口，拒绝用户信息、路径、查询、片段、百分号编码、反斜杠及控制字符，错误信息不回显原始 endpoint。

凭据审计确认生产路径不记录原始 JWT、share token、密码、S3 access/secret key 或文件正文；共享集成测试证据写入器现会递归脱敏结构化字段，并清理纯文本中的认证头、JWT、AWS access key、URL 密码和敏感环境变量。认证生命周期测试不再输出密码，普通 HTTP 证据不再绕过脱敏入口；仅显式用于合成脱敏回归的 raw evidence 接口保留原样写入能力。

上传授权以 `(upload_id, user_id)` 为数据库查询边界，并在分片落盘、组装、取消和清理副作用之前失败。真实双用户 HTTP 集成覆盖未缓存及 5 秒上传任务缓存命中后的 chunk/complete/cancel 共 7 次越权尝试，均返回统一 `400 + 50008`，数据库任务、租约、分片、文件、内容、任务队列和本地对象均保持不变；所有者取消只释放一次配额，随后重复取消仍不泄露任务存在性。既有刷新令牌轮换、完成幂等和精确路由限流测试继续覆盖重放与 `429` 合同；Redis 限流当前仍采用可用性优先的 fail-open，生产需通过 Redis 高可用、告警和边缘限流降低依赖故障窗口风险。

安全聚焦 CTest 63/63 通过；完整构建成功，完整 CTest 共 1381 项：1375 通过、6 项环境门控跳过、0 失败，总耗时 427.07 秒；OpenSpec 严格校验 24/24 通过。允许私网 endpoint 是部署能力而非动态请求能力，目标环境仍须用 MinIO IAM 最小权限、TLS、DNS 固定和出站网络策略约束 SSRF 影响面，并完成环境门控的真实 S3/多实例测试；这些残余验收不据此勾选 Phase 9 或最终 Definition of Done。

### 15.3 数据一致性审计记录（2026-07-21）

`BackupRestoreReconciliationIntegration` 在唯一源库和空恢复库之间真实执行 custom-format `pg_dump/pg_restore`，精确保留预置管理员加 2 个夹具用户、3 条文件、3 条内容、1 条回收站引用和 2 个活动上传。恢复库另加入 2 个有活动分片行引用的 local staging 对象，以每页 1 条让真实 Worker 完成 `contents/users/staging/final` 四 scope 的全部 continuation；干净扫描、故障扫描和修复复扫均只按各自 `scan_id` 验收。故障扫描精确持久化 `content_ref_count_mismatch`、两类 quota mismatch、缺失/大小错误 final Blob、`orphan_staging_object` 和 `orphan_final_blob` 共 7 类 finding；修复后 finding 全部由新扫描消解，文件数、配额、ref_count、完整哈希及 staging/final 双向 DB/对象集合重新一致。聚焦 CTest 1/1 通过（4.14 秒）。

S3 协议审计在父提交 `f11d693` 的候选工作树上，以 Moto 5.2.2 和 6 MiB/6 分片 payload 完成一次真实 multipart 组装、promotion 与 Worker cleanup。provider 清单在组装前看到 6 个 chunk，完成后看到 6 个 chunk、1 个 assembled 和 1 个 final 对象；cleanup 后 staging 对象与 `list_multipart_uploads` 未完成项均为 0，final 保持 1 个且完整 SHA-256 通过。数据库同时精确为 1 条文件、1 条内容、1 条已完成 S3 任务、`ref_count=1`、`storage_used=6291456`、`storage_reserved=0`，API/Worker 本地暂存均为 0；`MultipartRecoveryIntegration` 另行 1/1 通过（0.67 秒），证明持久化 `multipart_abort` 可恢复。`0600` 证据 `.sisyphus/evidence/data-consistency-s3-audit-2026-07-21.json` 的 SHA-256 为 `cba9498bb2665cb760075802f12720196d6dcf2e9ed1d12122bf83f7e73a341e`。

Python 语法检查和 OpenSpec 严格校验 24/24 通过。完整 CTest 首轮共 1381 项：1374 通过、6 项环境门控跳过、1 项失败，总耗时 428.72 秒；失败仅为既有 `SafetyUploadInvariantsIntegration` 中 487 条断言的一条共享库总预留量时序断言，该用例随后聚焦复跑 1/1 通过（105.60 秒）。因此 Phase 10 的“完整测试、多实例测试和压力测试”仍保持未勾选；Moto 证据记录 `git.dirty=true`，只用于一致性协议审计，不是性能基线，也不替代目标 MinIO/云 S3 的 lifecycle、multipart inventory、恢复演练或 Phase 9/最终 Definition of Done。

### 15.4 共享配额长等待断言收敛记录（2026-07-21）

`SafetyUploadInvariantsIntegration` 的认领进程死亡、组装后进程死亡和续租网络分区场景均会等待 30 秒 PostgreSQL 租约自然过期。旧断言把等待前的共享管理员总 `storage_reserved` 当作等待后固定基线，因此会把同用户无关上传的合法过期误判为目标接管重复释放。现在每个场景以单条 PostgreSQL 语句快照同时读取用户配额与所有 `Pending`/`Finalizing` 任务预留，断言两者残差在故障前后不变；目标任务的 `reserved_bytes`、终态、已用配额增量、文件/内容/ref_count 和 cleanup 唯一性断言全部保留。这个口径允许无关任务正常收敛，但任何目标任务少释放或多释放仍会改变残差并使测试失败。

首次改为绝对汇总对账时，三个断言都以同一个 5152 字节差额失败，确认共享库在本用例开始前已有历史残差，不能归因于当前接管。残差前后口径下聚焦回归 490/490 断言通过，CTest 1/1 通过（105.62 秒）；完整 CTest 共 1381 项：1375 通过、6 项常规环境门控跳过、0 失败，总耗时 437.68 秒。Python 语法检查和 OpenSpec 严格校验 24/24 通过。本轮没有执行目标环境多实例或压力门控，因此 Phase 10 第 822 项和最终 Definition of Done 仍保持未勾选。隔离恢复和 contract 准入测试继续要求配额绝对对账为零，本修正没有放宽该验收。

### 15.5 管理员错误码合同同步记录（2026-07-21）

`ErrorCode.hpp` 的 7 个管理员错误码原本已有稳定数值和 HTTP 状态映射，但没有默认消息映射；因此 `AdminService` 的默认 `ErrorInfo` 以及 `AdminAuthFilter` 的 80001 响应会回退为 `Unknown error`。现在 80001–80007 均有可读默认消息，现有 `AdminErrorCodeContract` 同时锁定数值、HTTP 状态和消息，过滤器响应测试也精确断言 80001 的实际信封。`02-API接口设计.md` 1.4 的权威表已补齐管理员域，覆盖运行时全部 45 个 `Code` 枚举值；OpenSpec 明确禁止已知错误回退未知消息。

clang-format、后端完整构建和聚焦 CTest 4/4 通过；完整 CTest 保持 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 430.44 秒；OpenSpec 严格校验 24/24 通过。本轮只完成 Phase 10 第 821 项中的错误码表子项，尚未重新审计全部架构图、部署样例和运维手册，因此第 821 项保持未勾选；目标环境多实例和压力门禁也未执行，第 822 项保持未勾选。

### 15.6 README 分布式部署样例同步记录（2026-07-21）

根 README 原本仍声称最终 blob 切换到 S3 后，上传分片和组装文件固定使用 API 节点本机 `temp_upload_path`，且快速开始缺少 `upload_staging_backend`、上传创建回滚开关和进程角色，容易把旧的节点亲和拓扑当作当前分布式方案。现在 README 明确区分 `all + local/local` 单进程开发与 `api/worker + s3/s3` 分布式目标，补齐本地多实例 Compose 入口、生产 `DISK_SECURE_MODE=true`/TLS/私网边界、当前项目结构、持久 Worker 清理和分片组装限流说明；本地 HTTP MinIO Compose 被明确限定为本地或隔离预发布验收，不作为可直接上线的生产配置。

现有 `DistributedTopologyContract` 新增 README 回归断言，要求保留 local/local 与 s3/s3 两套语义、权威 `docker-compose.distributed.yml` 入口和生产安全门禁，并拒绝旧的本机暂存断言。脚本直接执行与聚焦 CTest 均为 1/1 通过；完整 CTest 共 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 428.96 秒；OpenSpec 严格校验 24/24 通过。本轮只完成 Phase 10 第 821 项中的 README 与部署样例入口子项，架构图、运维手册和全局测试数量仍待逐项审计，因此第 821 项保持未勾选；未执行目标环境多实例和压力门禁，第 822 项也保持未勾选。

### 15.7 分布式架构与数据流文档同步记录（2026-07-21）

`00-系统概述.md` 的总体架构原本仍把当前服务画成单实例进程内 `ScheduledTasks`、已删除的文件组装池和本地上传暂存，上传/下载时序也让 API 直接访问文件系统；`01-功能需求规格.md` 仍允许重复分片覆盖、完成请求同步删除分片、零引用时立即删除物理文件，并把回收站清理写成 API 每日定时器。现在总体图使用同一制品的 `api`/`worker`/`all` 角色、PostgreSQL 租约与持久任务、`UploadStagingStorage`/`BlobStore` 和仅限本机容量的 `AssemblyConcurrencyLimiter`；上传、下载、去重、回收站清理及单机/分布式部署图同步为不可变分片、内容寻址 Blob、事务内 `staging_cleanup`/`blob_gc` 入队和 Worker 租约接管。

权威 `backend-refactor-decisions.md` 同步记录已经落地的独立 final/staging 后端、任务固化 backend/prefix、S3 multipart 组装、`multipart_abort` 和结果未知时保留已提升 final Blob 供幂等重试/对账，不再把 S3-native staging 列为未来需求或要求立即补偿删除。`DistributedTopologyContract` 对三份当前架构文档增加必需/禁用标记；Python 语法检查、脚本直接执行和聚焦 CTest 1/1 通过，完整 CTest 共 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 428.18 秒，OpenSpec 严格校验 24/24 通过。当前环境没有 Mermaid CLI，未完成图表渲染验收；部署运维指南其余图表和全局测试数量仍待审计，因此 Phase 10 第 821 项保持未勾选，目标环境多实例与压力门禁也未执行，第 822 项保持未勾选。

### 15.8 活跃测试清单与数量同步记录（2026-07-21）

`06-单元测试用例.md` 的活跃清单已映射到实际存在的状态机、任务队列、S3 存储和跨实例撤销测试入口，移除不存在的 `S3UploadStaging_test.cpp`、`test_token_revocation_cluster.py` 以及已经失效的 ADR-002/分享限流“待实现”标记。Redis 测试表不再维护易漂移的预计用例数，改为列出可执行单元与集成入口；`04-系统测试计划.md` 将仓库级合同已实现和目标 MinIO/多实例/故障/性能门禁待执行明确分层。

`DistributedTopologyContract` 现会验证文档入口实际存在、禁止旧状态与虚构路径回归，并固定顶部最近验证的 CTest 数量及 `total = passed + skipped` 对账。Python 语法检查、脚本直接执行和聚焦 CTest 1/1 通过；CMake 配置与完整构建通过，完整 CTest 共 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 427.18 秒；OpenSpec 严格校验 24/24 通过。本轮完成 Phase 10 第 821 项中的测试清单与数量子项；部署运维指南余项和 Mermaid 渲染验收尚未完成，因此第 821 项保持未勾选，目标环境多实例与压力门禁未执行，第 822 项也保持未勾选。

### 15.9 角色化服务管理运维同步记录（2026-07-21）

旧 Linux/Windows 服务示例向后端传入未实现的 `-c` 参数，且只定义单一 `disk` 服务、本机 final/暂存目录和无效 HUP reload，会让生产副本仍读取工作目录下的 `config.json` 并混淆 API/Worker 所有权。现在仓库新增 `deploy/systemd/disk@.service`，固定用 `DISK_CONFIG_FILE=/etc/disk/config.distributed.json` 选择配置，通过 `/etc/disk/instances/%i.env` 逐副本注入角色与监听地址，租约 `instance_id` 由每次启动的进程生成新 UUID，避免把稳定副本槽位误当作跨重启 owner。模板同时固定 `Restart=on-failure`、40 秒停机窗口、root 读取的公共 Secret 文件和只允许 `/var/lib/disk`/`/var/log/disk` 写入的 systemd 加固边界。NSSM 示例同样拆分 `DiskApi`/`DiskWorker`，不再使用 Bash 续行或 CLI 配置参数，也不固化跨重启的 owner。

部署指南同步为 S3 final + S3 staging 的生产目录图，区分 Drogon HTTP 请求临时目录与权威业务暂存；安全配置统一使用代码实际读取的 `DATABASE_PASSWORD`，PostgreSQL/Redis 每进程连接池回到已验证的 8/4 基线。角色化日常启停、readiness 监控、备份/恢复变量、manifest 迁移和二进制回滚命令已统一；回滚只覆盖明确备份的制品，不再递归删除 `/opt/disk`。`DistributedTopologyContract` 固定了配置入口、连接预算、双角色命令、单元加固与旧写法禁用集。

Python 语法检查、合同脚本直接执行和聚焦 CTest 1/1 通过；`systemd-analyze verify` 在隔离根目录中通过且无告警，CMake 配置与完整构建通过。完整 CTest 共 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 427.87 秒；OpenSpec 严格校验 24/24 通过。本轮完成 Phase 10 第 821 项中的角色化服务管理子项；Nginx 与运维指南其余图表的全量审计、Mermaid 渲染验收仍未完成，因此第 821 项保持未勾选；目标 MinIO/云 S3、多实例与压力门禁未执行，第 822 项也保持未勾选。

### 15.10 Nginx 与 TLS 入口运维同步记录（2026-07-21）

旧运维指南同时让 Drogon 和 Nginx 监听公网 `443`，并内嵌一份与运行资产不同的 `disk_backend` 配置：它缺少 `X-Request-Id`，为下载维护了绕过公共转发头的特殊 `location`，超时和 upstream 失败参数也与 Compose 入口不一致。现在 OpenSpec 与部署指南统一为 Nginx 单点终止公网 TLS，API 只监听受私网或容器网络保护的 HTTP `8080`；HTTP 使用固定 `server_name` 的 `308` 跳转，complete 响应未知继续通过原 `upload_id` 的幂等业务恢复处理，而不依赖代理重放。

仓库新增 `deploy/nginx/includes/upstream.inc` 和 `proxy-server.inc`，将 `least_conn`、可切换 API 池、请求头、`20m` 分片请求上限、3/330 秒超时、流式请求/响应、公网 `/metrics` 拒绝和不含 `non_idempotent` 的重试策略收敛为一份共享合同。Compose 明文入口与新增的 `deploy/nginx/disk-tls.conf.example` 均只选择监听器并加载该合同；`deploy/nginx/upstreams/production.example.inc` 给出两台私网 API 基线，生产 TLS 站点不再复制代理指令或下载特例。`ApiPoolRolloutContract` 同时锁定共享 include、只读 Compose 挂载、TLS/双栈监听、证书路径、308 跳转和生产 upstream；`DistributedTopologyContract` 跨文件验证无 cookie/ip-hash、必需转发头、关闭请求缓冲及运维指南禁用旧配置。

Python 语法检查、两个合同脚本直接执行、聚焦 CTest 2/2、CMake 配置与完整构建均通过；完整 CTest 共 1381 项，其中 1375 项通过、6 项按环境门控跳过、0 失败，总耗时 427.50 秒；OpenSpec 严格校验 24/24 通过。当前主机没有 Nginx、Docker 或其他容器运行时，因此只完成仓库级静态合同，目标环境仍须执行真实 `nginx -t`、证书续期 dry-run、HTTPS readiness、`/metrics` 拒绝和双 API 随机路由。本轮完成 Phase 10 第 821 项中的 Nginx/HTTPS 运维子项；运维指南其余图表的全量审计和 Mermaid 渲染仍未完成，第 821 项保持未勾选，目标 MinIO/云 S3、多实例与压力门禁未执行，第 822 项也保持未勾选。

### 15.11 迁移运维与图表收尾记录（2026-07-21）

`05-部署运维指南.md` 3.4.1–3.4.6 原本仍描述仓库中不存在的 `V001_forward.sql`、MySQL 风格 `BIGINT UNSIGNED`/`DATETIME` 类型和“活跃预留/分片必须全部为零”假设。现已按真实 `sql/migrations/manifest.tsv` 收敛为 V002 兼容基线上的 V003/V004 顺序迁移：单实例 migration Job 调用 `scripts/migrate-db.sh`，每版本在单事务中取 advisory lock、验证 SHA-256 并记入 `schema_migrations`；某一版本失败时保留已提交 manifest 前缀并可幂等续跑。运维 SQL 显式核对两张新表、18 个命名约束、6 个关键索引和 S3 暂存定位，且保留 V002 旧进程 expand 后 local 任务 `staging_prefix IS NULL` 回退 `temp_path` 的兼容语义。

迁移流程图现在明确备份/空隔离恢复演练、manifest/checksum、Worker/API 顺序、旧连接回收、四域对账与 `schema_action=preserve_expand`；生产目录图现在展示 Nginx TLS/`least_conn`、双 API、双 Worker、各节点互不共享的本机临时目录/日志，以及共享 PostgreSQL、Redis 和 S3 final/staging。`DistributedTopologyContract` 固定了迁移入口、回滚边界、新图关键节点、两张运维 Mermaid 清单，并禁止 V001/MySQL 旧文本和单进程目录图回归。

Python 语法检查、合同脚本直接执行、V003/V004 对账 SQL 四项计数 0/0/0/0 和聚焦 CTest 5/5 均通过；使用 Mermaid CLI 11.16.0 与 Chromium 148 对 `docs/design/` 五份活跃文档中的 14/14 张图完成实际 SVG 渲染，全部非空。CMake 配置和完整构建通过；完整 CTest 共 1381 项，1375 通过、6 项环境门控跳过、0 失败，总耗时 427.74 秒；OpenSpec 严格校验 24/24 通过。至此 Phase 10 第 821 项所列的架构图、部署样例、运维手册、错误码表和测试数量全部完成；目标 MinIO/云 S3、真实 Nginx/多实例与压力门禁未执行，第 822 项仍保持未勾选。

## 16. 最终 Definition of Done

- [ ] 两个及以上 API 实例通过无粘性负载均衡提供全部现有后端能力。
- [ ] 同一上传的每个请求随机命中不同实例仍可正确完成。
- [ ] API/Worker 在关键阶段被强制终止后，任务可自动接管或由客户端幂等重试恢复。
- [ ] 并发重复请求不会产生重复文件、重复配额结算、错误 ref_count 或误删 Blob。
- [ ] PostgreSQL、Redis、S3/MinIO 均有明确高可用入口、容量预算、备份与恢复演练。
- [x] 集群级周期任务不会随 API 副本数重复执行。
- [ ] liveness/readiness、日志、指标、告警和运维诊断覆盖 API、Worker、数据库、Redis、S3 和任务队列。
- [ ] 所有文档先于对应实现更新，API、数据库、部署和测试描述与最终行为一致。
- [ ] 完整 C++、Python 集成、多实例、S3、迁移、故障注入和压力测试通过。
- [ ] 生产不再创建本地暂存任务，所有临时迁移代码均已删除或有明确删除版本。

## 17. 后续可选演进（不阻塞本次重构）

- [ ] 根据监控数据评估客户端直传 S3 的版本化 API 与短期签名 URL。
- [ ] 根据实际团队边界和独立扩容需求评估拆分上传/下载 Worker，而不是按名词拆微服务。
- [ ] 评估 CDN、边缘下载和对象存储跨区域复制。
- [ ] 评估只读副本承载非权限关键的查询与报表。
- [ ] 只有单 PostgreSQL 写入能力成为已测量瓶颈后，再评估分区、分片或分布式 SQL。

## 18. 推荐执行顺序

```text
Phase 0 文档与基线
    -> Phase 1 Schema/迁移
    -> Phase 2 状态机与事务正确性
    -> Phase 3 S3 原生暂存
    -> Phase 4 Worker/任务队列
    -> Phase 5 缓存与认证一致性
    -> Phase 6 HA 部署
    -> Phase 7 可观测性
    -> Phase 8 全面验证
    -> Phase 9 灰度迁移
    -> Phase 10 清理收尾
```

在 Phase 2、Phase 3、Phase 4 未完成并通过多实例故障测试前，不应把当前后端标记为“支持生产级分布式部署”。
