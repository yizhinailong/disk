# 分布式后端重构 TODO

> 状态：实施中
>
> 目标：将当前 Drogon 后端重构为可无粘性会话横向扩容、可故障恢复的模块化单体。
>
> 原则：本文件是执行索引，不替代 `docs/design/` 中的权威设计。每一阶段必须先更新对应设计/API/数据库/部署/测试文档，再修改代码。
>
> 最近验证（2026-08-01，当前候选六项本机环境门禁全量复验）：候选基线 `4d76b9d9` 加本批 S3 门禁 Redis 隔离修复的带六项门禁完整 CTest 共 1451 项，1450 通过、仅 `DistributedFlowIntegration` 1 项因当前机器没有 Docker/Podman/nerdctl 按环境门控跳过、0 失败，总耗时 683.59 秒。首轮同序套件暴露并修正 `S3AppFlowIntegration` 继承共享 Redis 上传限流计数的夹具隔离缺陷；修复后该门禁在限流与并发上传压力用例之后 11.20 秒通过，本地双 API/双 Worker 拓扑 20 项检查 140.26 秒通过。四份 `0600` 原子证据已绑定 SHA-256，受管进程与本批临时目录已清理。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；本机进程拓扑不替代当前候选的 Dockerfile/Compose 构建，也不替代目标 Nginx、TLS/KMS、高可用端点、独立故障域、长稳、压力、真实迁移数据、兼容路径退役或预发布灰度，Phase 3/6/9/10 与最终 DoD 仍保持未勾选。

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
- [x] 任意步骤重试不会造成负配额、重复文件、错误 ref_count 或误删 Blob。
- [x] 所有状态机和事务竞态有数据库级集成测试，不只依赖源码字符串断言。

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
- [x] 验证每个 API 实例都能从 S3/MinIO 读取任意 Blob，不依赖本地缓存文件。
- [x] 对对象缺失、长度不一致和 Range 上游中断返回一致错误并记录可对账信息。
- [ ] 评估后续增加短期签名下载 URL，但不作为本轮多实例上线前置条件。

### 8.5 Phase 3 验收

- [x] 初始化、所有分片和完成请求逐次轮询到不同实例仍能成功。
- [x] API 节点删除本地 `temp_upload_path` 后，S3 暂存流程不受影响。
- [x] 大于单次 copy 限制的文件可完成组装/提升，内存占用保持有界。
- [x] 对象存储故障不会留下无法识别或无法清理的 multipart/staging 工件。

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

- [x] 提供 `docker-compose.distributed.yml`：PostgreSQL、Redis、MinIO、两个 API、两个 Worker 和无粘性负载均衡器；最新候选镜像的真实启动与完整 Compose 流程已由 15.51 记录。
- [x] 四个应用实例使用同一 `disk-distributed:local` 镜像，通过 `DISK_PROCESS_ROLE` 和 `DISK_INSTANCE_ID` 区分职责。
- [x] 分布式 JSON 的密码字段为空；S3、数据库、Redis 密钥只从环境或 Secret 注入，占位 `.env.distributed` 不入库。
- [x] 启动校验覆盖 role、S3 staging/final 组合、bucket、endpoint scheme、TLS 校验和成对 S3 凭据。

### 11.2 PostgreSQL

- [x] 采用主库写入端点和经过演练的故障切换方案；第一期不做业务分片。
- [x] 本地双 API/双 Worker 固定 PostgreSQL 32、Redis 16 条应用连接预算并保留运维余量；扩容公式已写入部署指南。
- [x] 区分事务池模式限制，确认 Drogon ORM、prepared statement 和 advisory lock 兼容性。
- [x] Compose PostgreSQL 显式设置 statement、lock、idle transaction 超时，避免故障任务长期占用资源。
- [x] 建立备份、时间点恢复和恢复演练流程。
- [x] 只在可证明安全的只读查询上评估读副本；当前查询白名单为空，上传状态、权限及全部既有业务/运维判断只读主库写端点。

### 11.3 Redis

- [ ] 使用私网高可用端点并开启认证/TLS（目标环境支持时）。
- [x] 验证故障切换期间连接重建、命令超时、Lua/CAS、SCAN 和 key TTL 行为。
- [x] 分布式配置默认每实例 4 条 Redis 连接，双 API/双 Worker 总预算 16 条；扩容必须重新核算。
- [x] 备份或持久化要求按 refresh token/撤销语义明确，不把 Redis 当作可随意清空的纯缓存。

### 11.4 S3/MinIO

- [x] 仓库 MinIO 样例初始化会幂等开启并回读 `Enabled` bucket 版本控制，且应用凭据显式拒绝版本级删除；该仓库门禁不代表下述生产整项完成。
- [ ] 生产 bucket 开启必要的版本、加密、TLS、最小权限和生命周期规则。
- [x] API/Worker 凭据仅允许所需前缀和操作，迁移工具使用独立临时权限。
- [x] 配置连接池、请求超时、重试预算和每实例并发上限。
- [x] 评估 MinIO 自建部署的磁盘冗余、节点故障域和备份；不能用单节点 MinIO 宣称存储高可用。

### 11.5 负载均衡与发布

- [x] Nginx 采用无 cookie/ip-hash 的 `least_conn`，覆盖并传递客户端 IP、协议和代理生成的请求 ID。
- [x] Nginx 固定 20 MiB 请求体、330 秒转发超时、关闭请求缓冲，且未启用 `non_idempotent` 重放。
- [x] API/Worker 支持 SIGTERM 优雅关闭和有界 drain。
- [x] 数据库迁移、API、Worker 按兼容顺序滚动发布。
- [x] 配置最小副本、反亲和/故障域、资源 requests/limits 和扩缩容指标。
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

### 11.12 Phase 6 PostgreSQL PgBouncer 事务池兼容性验收记录（2026-07-21）

设计与部署合同现只准入 PgBouncer 1.25.2 及以上的 `pool_mode=transaction`，要求 `max_prepared_statements` 非零并保持 `server_reset_query_always=0`。允许范围是 Drogon ORM/`TransactionRunner`、协议级命名 prepared statement、`pg_advisory_xact_lock`、`SET LOCAL` 和同一事务内的 `CREATE TEMP TABLE ... ON COMMIT DROP`；静态拓扑合同拒绝会话级 advisory lock、SQL 级 `PREPARE`/`DEALLOCATE`、`LISTEN`/`UNLISTEN`、holdable cursor、会话 SET/RESET 依赖和持久临时表。

本轮先以官方 SHA-256 校验的源码临时构建 PgBouncer 1.25.2，再由 `test_pgbouncer_transaction_pool.py` 启动它与唯一临时 PostgreSQL/Redis、两个真实 API。夹具把每 API 4 条、合计 8 条 Drogon 客户端连接复用到精确 2 条 PostgreSQL 后端连接；跨实例注册/profile、ORM 父子文件夹创建、`TransactionRunner` 子树 rename 和 breadcrumb 回读均成功。`SHOW STATS` 最终记录 client parse 26、server parse 19、bind 54；双代理客户端证明同一 `pg_advisory_xact_lock` 在持有事务提交前拒绝竞争者、提交后立即释放，事务内 `SET LOCAL` 与 `ON COMMIT DROP` 临时表也成功。测试先完成应用连接/plan 取证并停止双 API，再执行双连接锁探针，避免把 2 条后端预算耗尽误判为锁失败。

Python 语法检查、CMake 配置和完整构建通过；PgBouncer/拓扑聚焦 CTest 2/2 通过（3.67 秒），真实脚本另连续重复 3/3 通过；完整 CTest 共 1392 项，1386 通过、6 项既有环境门控跳过、0 失败，总耗时 483.59 秒；OpenSpec 严格校验 24/24 通过。`0600` 原子证据 `.sisyphus/evidence/pgbouncer-transaction-pool-summary.json` 的 SHA-256 为 `02e268afc4bbf8b1b4612af2db9292ca9e70aaa94cd5478d3052c6f9d0e85a30`，不含端口、路径、凭据或业务标识。本记录不支持被禁止的会话特性，也不替代生产 PgBouncer 认证/TLS、稳定写端点 HA、独立故障域、内存/连接容量和版本升级回归。

### 11.13 Phase 6 MinIO bucket 版本与权限门禁记录（2026-07-21）

`deploy/minio/provision.sh` 现在建 bucket 后、发放应用账号前幂等执行 `mc version enable`，并解析 `mc --json version info` 要求精确的 `Enabled` 状态。固定 MinIO `RELEASE.2025-04-22T22-12-26Z` 实测发现，仅仅不授予 `DeleteObjectVersion` 仍会让带 `VersionId` 的删除借用已允许的 `DeleteObject` 成功；因此应用 policy 对 `objects/*`/`staging/*` 增加精确的显式 `Deny s3:DeleteObjectVersion`，同时保留应用正常读写、复制、multipart 和普通删除能力。

`S3ProvisioningIntegration` 在 SHA-256 已复核的固定 MinIO/mc 二进制上 1/1 通过（2.02 秒），11 项检查覆盖二次幂等初始化、版本状态/lifecycle 回读、stale multipart 清理、受限数据面、越权管理/版本删除拒绝、删除标记/历史版本保留和初始化身份最终清理。`0600` 原子证据 `.sisyphus/evidence/s3-provisioning-summary.json` 的 SHA-256 为 `5a5bfdd48eb448d460391cd8183798f92a7e277ee4681de6654a41c2c52cada0`。完整 CTest 共 1392 项，1386 通过、6 项预期环境门控跳过、0 失败，总耗时 481.00 秒；OpenSpec 严格校验 24/24 通过。该单节点 HTTP 样例不替代目标 bucket 的加密、TLS、非当前版本保留、备份/恢复、故障域与高可用验收，所以 11.4 生产整项仍保持未勾选。

### 11.14 Phase 6 MinIO 应用/迁移身份隔离门禁记录（2026-07-21）

对象存储身份现拆为 provisioning root、API/Worker 应用账号和可撤销的 final Blob 迁移账号。应用 policy 只覆盖 `objects/*`/`staging/*` 必要数据面并显式拒绝版本级删除；`deploy/minio/migration-policy.json` 只允许 `objects/*` 的读、写和 multipart，显式拒绝普通/版本删除且没有 staging、bucket 列举或管理权限。初始化脚本要求三组 access key 和 secret 两两不同，Compose 业务容器只接收应用凭据；迁移窗口关闭后由 `deploy/minio/revoke-migration-access.sh` 删除迁移用户，撤销任务拒绝接收应用凭据。

`scripts/migrate-final-blobs.py` 的 copy/cutover 只读取 `DISK_S3_MIGRATION_*` 或独立工作负载身份，并在首个 S3 请求前拒绝应用变量、残缺迁移 key 对和孤立 session token。`FinalBlobMigrationIntegration` 1/1 通过（11.47 秒），证明三种误配均为 0 个 S3 请求、无 checkpoint 且数据库不变；`S3LifecycleConfigIntegration` 1/1 通过（0.09 秒）。固定 MinIO/mc 的 `S3ProvisioningIntegration` 1/1 通过（2.58 秒），15 项检查证明应用能力不回退、迁移 final-only 读写/multipart、删除/越界/管理拒绝、撤销前应用凭据隔离和撤销后原迁移凭据失效。

`0600` MinIO 证据 `.sisyphus/evidence/s3-provisioning-summary.json` 的 SHA-256 为 `77de989d8d5e96c97d01360dbd1a408ad0b2a465baf9d4f60bd845e7016269e9`；去敏 copy 证据 `.sisyphus/evidence/final-blob-copy-summary.json` 的 SHA-256 为 `5657a11fc799eda67bf5125fa84620c495e7d76ffcbbb2f7d5b93343914a86e5`。完整 CTest 共 1392 项，1387 通过、5 项预期环境门控跳过、0 失败，总耗时 485.58 秒；OpenSpec 严格校验 24/24 通过。本记录只关闭 11.4 的身份/权限隔离子项，不替代目标 bucket 的加密、TLS、版本保留、备份/恢复、故障域或高可用验收，其余两项继续保持未勾选。

### 11.15 Phase 6 MinIO 自建冗余与备份评估记录（2026-07-22）

固定版本官方 sizing 的 4 server × 2 drive 生产下限只能容忍 1 个节点故障后继续写入，不能覆盖 Disk 要求的双节点完整故障域，因此明确拒绝。`deploy/minio/self-hosted-assessment.json` 将首期生产模型固定为 `managed_s3`，把单节点 Compose 标记为 `development_only`，并将自建候选提高为 6 server × 2 drive、3 个物理故障域各 2 节点、12 shard/4 parity；候选还要求专用同构主机、本地 XFS 数据盘、独立站点/供应商的版本化加密备份、默认不复制删除、对象版本 manifest、周期可读性校验和隔离恢复。

评估合同 SHA-256 为 `1e658ecb9fb10704a62d2dab8ba3d0aab906fb8536d817fd997cded4601ccd6e`。`S3LifecycleConfigIntegration` 1/1 通过（0.09 秒），锁定上游下限、候选拓扑、单节点夹具边界和九项准入证据；固定版 MinIO/mc 与 PgBouncer 显式门禁 2/2 通过（5.60 秒）。完整 CTest 共 1392 项，1387 通过、5 项目标环境门控跳过、0 失败，总耗时 470.88 秒；OpenSpec 严格校验 24/24 通过。

当前 `target_rpo_seconds`/`target_rto_seconds` 为空，全部目标证据未满足，自建状态保持 `not_approved`。本记录只关闭“评估”子项，不代表生产 bucket、自建对象存储、独立备份、故障切换或恢复演练完成；11.4 的生产 bucket 和 11.6 的目标环境验收继续保持未勾选。

### 11.16 Phase 6 Kubernetes 兼容发布与容量配置记录（2026-07-22）

`deploy/kubernetes/` 现将无敏感 platform、一次性 migration 和长驻 workloads 分为三层，仓库不包含 Secret。API/Worker 均以 2 副本起步，使用主机硬反亲和、至少两个 zone、固定数值 UID/GID 10001、PDB、角色探针、非 root/只读根文件系统和一进程滚动预留；4 副本上限与硬反亲和的滚动容量门禁要求至少 5 个可调度节点。HPA 限定在已测的 2–4 副本，API 使用 CPU/聚合在途请求，Worker 使用 CPU/集群最老可执行任务年龄。`deploy/prometheus/disk-autoscaling.yml` 提供对应的低基数记录规则。

`deploy/kubernetes/release-plan.json` 固定 preflight/capacity → expand migration → Worker → API → post-rollout 顺序。每次 migration overlay 必须使用唯一 `nameSuffix`，任一阶段失败即停；回滚先冻结新上传、按 API/Worker 逆序回滚镜像并保留 expand schema。运行时镜像增加 `psql`、受控迁移脚本、manifest 和 forward-only SQL，不包含破坏性 rollback SQL，因此同一候选镜像可执行发布前 Job，业务 Pod 仍不执行 DDL。

`DistributedTopologyContract` 递归解析 YAML/JSON、Dockerfile 和 Prometheus 规则，并与容量门禁聚焦 CTest 2/2 通过。使用官方 Kubernetes 1.30 客户端完成 platform、migration、worker、API 和 workloads 稳态聚合入口 Kustomize 渲染 5/5，四个发布入口共 11 个渲染资源的 1.30 严格离线 Schema 校验全部通过。完整构建无增量工作；完整 CTest 为 1385 通过、7 项环境门控跳过、0 失败，总耗时 481.47 秒；OpenSpec 严格校验 24/24 通过。

本记录关闭 11.5 的两个仓库级部署实现子项，不代表目标集群已发布。当前无 Kubernetes API Server 和容器运行时，未执行服务端 dry-run、候选镜像构建/拉取、External Metrics API、真实滚动或节点/故障域调度。这些与 11.6 的 API/Worker/依赖故障演练继续保持未勾选。

## 12. Phase 7：可观测性与运维工具

### 12.1 日志与追踪

- [x] 所有结构化日志包含 `request_id`、`instance_id`；上传相关日志增加 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。
- [x] 禁止记录 JWT、share token、密码、S3 凭据和文件正文。
- [x] 为 init/chunk/complete/download/cleanup 建立跨 API、DB、Worker、S3 的追踪关联。
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

### 12.6 结构化日志信封基础记录（2026-07-22）

stdout、旋转文件以及被捕获的 Drogon/Trantor 诊断现统一输出单行 `schema_version=1` JSON；固定字段包含时间、级别、来源、logger、消息和七个关联字段，未知关联值使用 JSON `null`。应用事件通过 `LogContext` 显式传入类型化关联值，框架事件不猜测协程上下文；配置冻结后全局注册 `instance_id`，HTTP 完成事件写入响应使用的真实 `request_id` 与低基数 `operation`。消息中的引号和换行由 JSON 序列化器转义，不再破坏 NDJSON 边界。

部署运维、系统测试和单元测试文档先行更新；`LogHelperTest` 锁定应用/框架信封、字段类型、空值和字符转义，现有 Worker 日志捕获也使用同一格式。聚焦日志/请求追踪/Worker CTest 26/26 通过，CMake 配置和完整构建通过；完整 CTest 共 1395 项，1388 通过、7 项环境门控跳过、0 失败，总耗时 466.69 秒；OpenSpec 严格校验 24/24 通过。init/chunk/complete/download/cleanup 各域日志尚未全部显式传播 `upload_id`、`job_id`、`lease_owner` 与 `state_version`，API、DB、Worker、S3 的端到端关联也未完成，因此 12.1 的两个总任务继续保持未勾选。

### 12.7 init/chunk 日志关联记录（2026-07-22）

部署运维、系统测试和单元测试文档先行固定 init/chunk 的字段与空值合同。`FileController` 现在从请求追踪属性创建显式 `LogContext`，将固定的 `upload_init`/`upload_chunk` operation 和真实 request ID 按值传过 Controller、`UploadService` 与 `UploadLifecycleService` 的协程边界，不依赖 thread-local。init 在任务存在前保持 `upload_id=null`，恢复或创建任务后补齐持久 ID，并在过期旧任务转入新建流程前清除旧 ID；chunk 以方法参数中的非空 upload ID 为规范值，使任务校验、尺寸/哈希检查、staging 写入和数据库记录结果使用同一关联上下文。

`SafetyUploadInvariantsIntegration` 以调用方指定的 `X-Request-Id` 分别触发超限 init 和分片尺寸失败，逐行解析受管后端 stdout NDJSON，精确验证 schema、source、instance/request/operation/upload 字段，并确认 `job_id`、`lease_owner`、`state_version` 为 JSON `null`；测试读取后端实际 `DISK_CONFIG_FILE`，避免配置替换时构造错误边界。聚焦日志/上传生命周期 CTest 68/68、真实 HTTP safety 集成 1/1、CMake 配置和完整构建均通过；完整 CTest 共 1395 项，1388 通过、7 项环境门控跳过、0 失败，总耗时 469.87 秒；OpenSpec 严格校验 24/24 通过。complete/download/cleanup 与 Worker/S3 的 `job_id`、`lease_owner`、`state_version` 端到端传播仍待后续最小批次，因此 12.1 的两个总任务继续保持未勾选。

### 12.8 complete 日志关联记录（2026-07-22）

部署运维、系统测试、单元测试和 OpenSpec 先行固定 complete 的类型化字段合同。`FileController` 从请求属性创建固定 `operation=upload_complete` 的 `LogContext`，在 DTO 得到非空 ID 后补齐 `upload_id`，并按值传过 `UploadService`、`UploadLifecycleService` 及续租、错误记录、对账等辅助协程。Lifecycle 只在 PostgreSQL 认领成功后填入真实 `lease_owner`，以每次 CAS 返回值推进 `state_version`；完成事务提交清空 owner 并记录完成更新后的版本，重放不伪造已清空的 owner。当前 cleanup/reconciliation 入队接口不返回持久行 ID，因此 complete 事件保持 `job_id=null`，不从 dedupe key 或消息文本推断。

`SafetyUploadInvariantsIntegration` 删除数据库已记录的 staging 对象后，以调用方 request ID 发起真实 complete 请求，逐行解析组装错误和 `[complete_upload]` 失败汇总 NDJSON，并查询失败后的 `upload_tasks`；两条 Lifecycle 事件的 request/instance/upload、lease owner 与 state version 均与响应和数据库行精确一致，job ID 为 JSON `null`。完整构建、Python 语法检查、聚焦 CTest 34/34 和真实 HTTP safety 集成 1/1 通过；完整 CTest 共 1395 项，1388 通过、7 项环境门控跳过、0 失败，总耗时 469.41 秒；OpenSpec 严格校验 24/24 通过。download/cleanup 及实际取得持久任务 ID 后的 Worker/S3 关联仍待后续最小批次，因此 12.1 的两个总任务继续保持未勾选。

### 12.9 Worker 持久任务日志关联记录（2026-07-22）

部署运维、系统测试、单元测试和 OpenSpec 先行固定 Worker 的类型化字段合同。`BuildStorageJobLogContext` 只从 PostgreSQL 已认领任务构造关联值：正数主键写入 `job_id`，非空 `locked_by` 写入 `lease_owner`，已知任务类型映射为有界 operation，未知类型统一映射为 `storage_job_unknown`；仅完整 payload 通过校验且聚合 ID 一致的 staging cleanup 任务携带业务 `upload_id`，multipart 远端 ID 不冒充业务上传 ID。Worker 没有 HTTP 请求和上传状态版本时保持 JSON `null`，执行汇总在结果回写后或无法确认继续持有所有权时清除 owner；认领、预检、心跳、执行、回滚及结果持久化等 Worker 本地事件均显式使用同一上下文。

`StorageJobWorkerTest` 覆盖六类已知任务、合法 staging cleanup、畸形 payload 和未知任务的映射/空值合同；`WorkerDrainTakeoverIntegration` 让 Worker B 真实接管 Worker A 的过期 Blob GC 租约，逐行解析 stdout NDJSON，验证开始事件携带数据库真实 job/owner、完成事件保留 job 并清空 owner，且两者 instance、operation 与空值字段准确。完整构建、Python 语法检查、聚焦 Worker/日志 CTest 24/24 和真实双 Worker 接管集成 1/1 通过；完整 CTest 共 1398 项，1391 通过、7 项环境门控跳过、0 失败，总耗时 464.85 秒；OpenSpec 严格校验 24/24 通过。download、cleanup API 生命周期及 S3 存储边界的端到端关联仍待后续最小批次，因此 12.1 的两个总任务继续保持未勾选。

### 12.10 download 日志关联记录（2026-07-22）

部署运维、系统测试、单元测试和 OpenSpec 先行固定下载类型化字段与空值合同。所有者和访客的下载信息、全量及 Range 路由现在均在参数解析前创建固定 `operation=download` 的 `LogContext`，按值传过 `FileQueryService`/`ShareService`、公共 `DownloadResponder`、完整性预检与对账、延迟流回调、下载统计和分享审计。下载不是上传或持久任务，顶层 `upload_id`、`job_id`、`lease_owner`、`state_version` 保持 JSON `null`；缺失、尺寸不符、打开失败或流中断的 reconciliation finding details，以及访客 `share_download` 审计 details，均保存同一 `request_id` 与 `operation`，非 HTTP 调用则显式保存 JSON `null`。

`DownloadResponderTest` 直接验证预检、打开失败和延迟流中断的上下文按值传播，`ShareAuditServiceTest` 锁定审计关联字段及空上下文 null 语义。`DownloadFlowIntegration` 以不同调用方 request ID 让所有者和访客下载同一缺失 final Blob，核对响应回显的 request/实际 instance、受管进程 NDJSON 的固定 operation 与四个类型化空值、每次请求后的 finding details，以及访客分享审计 details；`ShareAuditIntegration` 同时回归真实 PostgreSQL 审计写入。完整构建、Python 语法检查、聚焦 CTest 24/24、真实分享审计集成 1/1 和下载流集成 1/1 通过；完整 CTest 共 1398 项，1391 通过、7 项环境门控跳过、0 失败，总耗时 465.62 秒；OpenSpec 严格校验 24/24 通过。cleanup API 生命周期与 S3 适配器内部 SDK 日志的显式关联仍待后续最小批次，因此 12.1 的两个总任务继续保持未勾选。

### 12.11 cleanup 日志关联记录（2026-07-22）

部署运维、系统测试、单元测试和 OpenSpec 先行固定 cleanup 的类型化字段与空值合同。手动过期清理的精确路由现从通用 admin 分类为低基数 `operation=cleanup`，`AdminController` 在任何清理逻辑前从请求属性创建 `LogContext`，并按值传过 `CleanupService`、过期回收站分页/永久删除、`ContentService` 的引用计数与 Blob GC 入队，以及过期上传 Lifecycle。手动批次事件保持 `upload_id/job_id/lease_owner/state_version=null`；逐项 Lifecycle 只用数据库行的真实上传 ID 补全 `upload_id`，入队接口未返回持久主键时不从 content ID、aggregate ID 或 dedupe key 伪造 `job_id`。过期上传/回收站 Worker 也将已认领的真实 job/owner 和固定 operation 传入同一内部服务，不暗中继承 HTTP request 或伪造 state version。

`MetricsServiceTest` 锁定精确 cleanup 路由与通用 admin 路由的分类边界，清理/上传仓储源码合同同步锁定上下文传播。`SafetyUploadInvariantsIntegration` 以调用方指定的 `X-Request-Id` 真实过期一个上传，逐行解析 Controller、TrashService 和上传清理批次 NDJSON，核对响应同一 request/实际 instance、固定 operation 及四个 JSON `null`；直接查询受管日志也确认该唯一 request ID 的七条清理事件未误命中历史记录。完整构建、Python 语法检查、聚焦 CTest 49/49 和真实 HTTP safety 集成 1/1（脚本内 503 项断言）通过；完整 CTest 共 1398 项，1391 通过、7 项环境门控跳过、0 失败，总耗时 469.67 秒；OpenSpec 严格校验 24/24 通过。S3 适配器内部 SDK 日志仍是未完成的独立存储边界，因此 12.1 的两个总任务继续保持未勾选。

### 12.12 S3 SDK 日志关联记录（2026-07-22）

部署运维、系统测试、单元测试、ADR 和 OpenSpec 先行固定 S3 SDK 的类型化关联、固定语汇、级别与脱敏合同。`LogContext` 现在按值穿过 `UploadStagingStorage`/`IBlobStore`、`S3ObjectStorage` 阻塞工作队列和 `IS3Client`；分片写入、完成组装/提升、下载读取、staging cleanup、multipart abort、Blob GC 与存储对账均保留调用方已有的业务 operation 和类型化字段。AWS 适配器只从编译期枚举生成 12 个 `sdk_operation` 和 9 个 `outcome`，每次最终 SDK 返回同时结束无采样依赖指标并记录结果；`success/not_found/conflict` 固定为 `DEBUG`，其他结果固定为 `WARN`。结果事件和 Blob 删除重试日志不记录 bucket、endpoint、对象 key/prefix、远端 multipart ID、continuation token、凭据、签名、正文或 SDK 异常正文。

`S3ClientTest` 锁定全部操作/结果名称、`INFO` 下预期结果丢弃、失败 `WARN` 及六个上下文字段；`S3ObjectStorageTest` 通过内存 client 验证完整上下文跨过真实阻塞队列，`StorageJobWorkerTest` 验证已认领 cleanup/abort 任务把数据库 job/owner 和合法业务 upload ID 传入存储边界。完整构建、聚焦 S3/Worker/对账/下载 CTest 89/89 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1402 项，1395 通过、7 项环境门控跳过、0 失败，总耗时 477.76 秒。真实 S3/MinIO 与分布式拓扑门控未在本机执行；取消上传、本地组装等历史日志也尚未全部改为调用方上下文，因此 12.1 的两个总任务继续保持未勾选。

### 12.13 本地组装日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定本地 staging 组装的日志合同。`LocalFileStorage::AssembleChunks` 现将调用方 `LogContext` 用于既有的准入、容量拒绝、开始、失败、完成及耗时事件，使完整上下文跨过阻塞文件系统队列；实现没有新增事件或改变级别，也不从 session upload ID、状态版本参数、路径、文件名或消息文本推断调用方未提供的字段。

`LocalFileStorageAssemblyLogTest` 通过真实临时文件分别覆盖组装成功和已登记分片被删除后的工作线程失败，逐行解析内存 sink 的 NDJSON：成功路径 4 条事件完整保留六个业务字段并保持 `DEBUG`，失败路径 3 条事件保留 request/operation、其余字段保持 JSON `null`，最终汇总为 `INFO`。测试清单合同同步到 1404 项；完整构建、Python 语法检查、聚焦本地组装 CTest 4/4、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1404 项，1397 通过、7 项环境门控跳过、0 失败，总耗时 484.42 秒。取消上传等历史日志仍未全部使用调用方上下文，因此 12.1 的两个总任务继续保持未勾选。

### 12.14 取消上传日志关联记录（2026-07-22）

API、数据库、部署运维、系统测试、单元测试和 OpenSpec 文档先行固定取消上传的状态迁移与类型化日志合同。`FileController` 在解析出非空上传 ID 后创建固定 `operation=upload_cancel` 的 `LogContext`，按值传过 `UploadService`、`UploadLifecycleService` 和 PostgreSQL 事务。生命周期层现在是取消决策的唯一权威：CAS 只匹配同一 `upload_id + user_id` 下未过期的 `InProgress` 任务，原子推进并返回 `state_version`；竞争失败后读取持久状态，已取消重放返回原版本且不再次推进。低频成功/重放汇总固定为 `INFO`，Controller/Service 细节保持 `DEBUG`；取消没有已认领持久任务，`job_id` 和 `lease_owner` 保持 JSON `null`，公开响应仍保持 `data=null`。

`UploadTaskRepositoryTest` 锁定版本推进、截止时间约束、持久状态回读、单一生命周期决策及上下文传播。`SafetyUploadInvariantsIntegration` 使用两个不同的调用方 request ID 真实执行首次取消与重放，逐行解析 stdout NDJSON，并核对数据库版本只增加一次、预留配额只释放一次、staging cleanup 任务只创建一条，以及两条 `INFO` 汇总的 request/instance/upload/version 和空 job/owner 精确一致。完整构建、Python 语法检查、聚焦取消仓储/生命周期 GTest 10/10、真实 HTTP safety 集成 1/1（脚本内 515 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1404 项，1397 通过、7 项环境门控跳过、0 失败，总耗时 464.22 秒。其他历史上传辅助日志和目标 S3/多实例环境门控仍未全部完成，因此 12.1 的两个总任务继续保持未勾选。

### 12.15 过期上传状态版本与日志关联记录（2026-07-22）

数据库、ADR、部署运维、系统测试、单元测试和 OpenSpec 文档先行固定过期迁移与日志合同。`UploadTaskRepository` 的单行条件更新是逐上传过期的唯一权威原语：只匹配 `id + InProgress + expires_at < NOW()`，原子执行 `state_version + 1` 并返回清理描述符与新版本；赢家在同一短事务释放 reserved quota、写入唯一 staging cleanup 任务并删除分片元数据。未使用且可能绕过逐任务副作用事务的批量更新原语已删除，候选分页只负责稳定读取；CAS 未命中不推进版本，也不重复任何副作用。

`UploadLifecycleService` 保留调用方 request/operation 和 Worker 的真实 job/owner，只在事务提交后将返回版本写入逐上传 `INFO` 成功汇总；竞争失败保持空版本并降为 `DEBUG`。手动 cleanup 批次事件继续保持 `upload_id/job_id/lease_owner/state_version=null`，其逐项成功事件只补入真实 upload/version；Worker 任务级版本仍为空，逐项事件不得从 scan/page/attempts/job generation 推断版本。公开清理响应合同未改变。

`UploadTaskRepositoryTest` 锁定版本 SQL、返回类型、单一过期原语及事务调用顺序；`SafetyUploadInvariantsIntegration` 通过真实 HTTP 手动过期清理查询迁移前后 PostgreSQL 行，验证版本精确增加一次，并在受管后端 stdout NDJSON 中定位同一 request/instance、真实 upload ID、提交版本和空 job/owner 的逐项成功事件。完整构建、Python 语法检查、聚焦过期仓储/生命周期 GTest 5/5、真实 HTTP safety 集成 1/1（脚本内 518 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1404 项，1397 通过、7 项环境门控跳过、0 失败，总耗时 481.89 秒。其他请求域的历史辅助日志和目标 S3/多实例环境门控仍未全部完成，因此 12.1 的两个总任务继续保持未勾选。

### 12.16 文件查询日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定文件查询的类型化日志与分类边界。HTTP 指标只将精确的 `/api/file/list`、`/api/file/search` 和单段无符号十进制详情路径归为低基数 `operation=file_query`；上传、下载、rename/move/copy/delete 及非数字详情路径均保持原分类，不会被查询标签吸收。公开 REST 路由、请求参数、响应信封和错误码没有变化。

`FileController` 在 DTO 或路径参数解析前从请求属性创建显式 `LogContext`，按值传入 `FileQueryService` 的列表、详情和搜索协程；Controller 与查询服务的既有请求级事件均使用同一 request/instance/operation。文件 ID、父目录 ID、搜索关键字、用户 ID 和缓存 key 只保留在消息中，不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，这四个非查询所有权字段保持 JSON `null`。

`MetricsServiceTest` 锁定查询与上传/下载/变更路径的分类边界，`FileQueryLogContextContractTest` 锁定 Controller/Service 六个代码区段的显式上下文传播。`SafetyUploadInvariantsIntegration` 使用三个不同的调用方 request ID，分别触发不存在父目录的列表失败、不存在文件的数字详情失败和非法搜索分页，逐行核对 Controller、查询服务及 HTTP 完成 NDJSON 的 request/instance、固定 operation 与四个空所有权字段。完整构建、Python 语法检查、聚焦 GTest 3/3、真实 HTTP safety 集成 1/1（脚本内 530 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1405 项，1398 通过、7 项环境门控跳过、0 失败，总耗时 484.63 秒。DTO 内部解析日志、文件变更/目录/分享/管理员等其他历史请求域及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.17 文件变更日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定文件变更的类型化日志与分类边界。HTTP 指标只将单段无符号十进制 rename、精确的 move/copy 路由以及 `/api/file`、`/api/file/delete` 两个软删除兼容路径归为低基数 `operation=file_mutation`；查询、上传、下载、非数字 rename 和未知文件路径不会被变更标签吸收。公开 REST 路由、请求方法、请求体、响应信封和错误码没有变化。

`FileController` 在 DTO 或路径参数解析前从请求属性创建显式 `LogContext`，按值传过 `FileMutationService` 的 rename、move、copy、delete 及其复制辅助协程；delete 继续将同一上下文传入唯一的 `TrashService::MoveToTrash` 子流程。文件、文件夹、内容、用户、配额和缓存标识只保留在业务参数或消息中，不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，这四个非变更所有权字段保持 JSON `null`。

`MetricsServiceTest` 锁定五个变更路径与查询/上传/下载/非数字路径的精确分类，`FileMutationLogContextContractTest` 锁定 Controller、变更服务和移入回收站代码区段的显式上下文传播。`SafetyUploadInvariantsIntegration` 使用四个不同的调用方 request ID，分别触发不存在文件的数字 rename、不存在目标目录的 move/copy 和不存在文件的软删除，逐行核对 Controller、变更服务、回收站子流程及 HTTP 完成 NDJSON 的 request/instance、固定 operation 与四个空所有权字段。完整构建、Python 语法检查、聚焦 GTest 4/4、真实 HTTP safety 集成 1/1（脚本内 546 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1406 项，1399 通过、7 项环境门控跳过、0 失败，总耗时 471.79 秒。共享 DTO、Repository、Quota/Content/Cache 边界及目录/分享/管理员等其他历史请求域和目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.18 文件夹日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定文件夹请求的类型化日志与分类边界。HTTP 指标只将精确的 `/api/folder/tree` 和单段无符号十进制 breadcrumb 路径归为低基数 `operation=folder_query`，将精确的 `/api/folder/create` 和单段无符号十进制 rename 路径归为 `operation=folder_mutation`；非数字 ID、额外路径后缀和未知文件夹路径保持 `other`。公开 REST 路由、请求参数、请求体、响应信封和错误码没有变化。

`FolderController` 在 DTO 或路径参数解析前从请求属性创建显式 `LogContext`，按值传过直接记录日志的文件夹 DTO、`FolderService` 及其父目录校验、重名检查、计数更新和所有权校验辅助协程；Controller、DTO 和服务的既有请求级事件均使用同一 request/instance/operation，rename 与 breadcrumb 的缺失目标路径也有可关联的服务层告警。文件夹、用户、路径、事务和缓存标识不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，这四个非文件夹所有权字段保持 JSON `null`；构造期初始化日志继续作为无 HTTP 上下文的进程事件。

`MetricsServiceTest` 锁定查询/变更路径及负边界，`FolderLogContextContractTest` 锁定 Controller、直接 DTO 日志、服务和辅助协程的显式传播，DTO 用例继续回归请求合同。`SafetyUploadInvariantsIntegration` 使用四个不同的调用方 request ID，分别触发缺失父目录的 tree、缺失目标的 breadcrumb、缺失父目录的 create 和缺失目标的 rename，逐行核对响应、Controller、DTO/服务及 HTTP 完成 NDJSON 的 request/instance、固定 operation 与四个空所有权字段。完整构建、Python 语法检查、聚焦 CTest 42/42、文件夹生命周期集成 1/1、真实 HTTP safety 集成 1/1（脚本本次实际 564 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1407 项，1400 通过、7 项环境门控跳过、0 失败，总耗时 471.65 秒。共享 `DtoBase`、Repository、`TransactionRunner`、Cache 边界，分享/管理员等其他历史请求域，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.19 回收站日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定回收站的类型化日志、分类和验收边界。由于 `GET /api/trash` 与 `DELETE /api/trash` 共用 path，而 HTTP 分类器不接收 method，`/api/trash`、`/api/trash/restore`、`/api/trash/delete` 和 `/api/trash/all` 四个精确 path 统一归一为低基数 `operation=trash`；额外后缀与未知路由保持 `other`，公开 REST 路由、请求方法、响应信封和批处理语义没有变化。

`TrashController` 在 DTO 解析或服务调用前从请求属性创建显式 `LogContext`，按值传过 Trash DTO、`TrashService` 的列表/计数/恢复/永久删除/清空以及恢复和删除辅助协程。回收站、用户、文件、目录、内容、路径、配额和缓存标识不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，四个非回收站所有权字段保持 JSON `null`；构造期初始化日志继续作为无 HTTP 上下文的进程事件。HTTP 完成日志保持全局级别策略：2xx 为 `DEBUG`，非 2xx 为 `WARN`，本批次未为验收成功请求而扩大生产 `INFO` 日志量。

`MetricsServiceTest` 锁定四个精确 path 与负边界，`TrashLogContextContractTest` 锁定 Controller、直接 DTO 日志、服务和请求级辅助协程的显式传播。`SafetyUploadInvariantsIntegration` 使用四个不同的调用方 request ID：回收站列表、缺失项恢复和缺失项永久删除核对生产 `INFO` 可见的 Controller/DTO/服务事件，非法恢复 DTO 核对 `WARN` HTTP 完成事件；破坏性 DeleteAll 不作用于共享测试用户。完整构建、Python 语法检查、聚焦 CTest 59/59、回收站生命周期集成 1/1、真实 HTTP safety 集成 1/1（脚本本次实际 582 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1408 项，1401 通过、7 项环境门控跳过、0 失败，总耗时 473.43 秒。共享 `TrashQuery`、`TransactionRunner`、Cache 边界，分享/管理员/认证/用户/系统等其他历史请求域，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.20 系统信息日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定系统信息请求的类型化日志、分类和验收边界。HTTP 指标只将精确 `/api/system/info` 归一为低基数 `operation=system_info`；`/api/system`、额外后缀和未知 system 路由保持 `other`，公开 REST 路由、鉴权、响应信封与存储统计失败时返回零值的既有降级语义没有变化。

`SystemController` 在认证属性检查前从请求属性创建显式 `LogContext`，按值传入 `SystemService`；服务继续传给 `system_get_info` StageTimer 和存储统计错误边界。通用 `StageTimer` 现在保留调用方显式提供的上下文并在析构事件中使用，不从阶段名、线程或耗时重建关联；用户、连接池与聚合统计只留在领域参数或消息中，`upload_id`、`job_id`、`lease_owner` 和 `state_version` 保持 JSON `null`。构造期服务初始化日志继续作为无 HTTP 上下文的进程事件。

`MetricsServiceTest` 锁定精确路径和三个负边界，`SystemLogContextContractTest` 以源码合同和内存 structured sink 锁定 Controller、服务、StageTimer 的显式传播及六个类型化字段保留。`SafetyUploadInvariantsIntegration` 使用两个不同调用方 request ID：已认证成功请求核对生产 `INFO` 的阶段计时事件，未认证请求核对 `WARN` HTTP 完成事件，并验证响应 instance 与四个空所有权字段。完整构建、Python 语法检查、聚焦 CTest 7/7、真实 HTTP safety 集成 1/1（脚本本次实际 590 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1409 项，1402 通过、7 项环境门控跳过、0 失败，总耗时 476.52 秒。分享、管理员、认证、用户等其他历史请求域和共享基础设施边界，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.21 用户资料日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定用户资料请求的类型化日志、分类和验收边界。HTTP 指标只将精确 `/api/user/profile`、`/api/user/password` 和 `/api/user/storage` 归一为低基数 `operation=user`；由于 GET/PATCH 共用 profile path 且分类器只接收 path，两种方法保持同一 operation。`/api/user`、额外后缀、未知 user 路由和相似的 users 路由保持 `other`，公开 REST 路由、鉴权、请求/响应信封、错误码和用户资料/密码/存储语义没有变化。

`UserController` 在认证属性读取或 DTO 解析前从请求属性创建显式 `LogContext`，按值传入 `ChangePasswordRequest`、`UpdateProfileRequest` 的直接解析/校验事件和 `UserService` 的资料读取、密码修改、资料更新、存储统计协程。用户、资料、配额和聚合统计值不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，四个非用户域所有权字段保持 JSON `null`；共享 `DtoBase` 未接收上下文时不从线程或字段重建关联，构造期服务初始化日志继续作为无 HTTP 上下文的进程事件。

`MetricsServiceTest` 锁定三个精确 path 与四个负边界，`UserLogContextContractTest` 锁定 Controller、User DTO 直接事件和四个服务协程的显式传播。`SafetyUploadInvariantsIntegration` 使用四个不同调用方 request ID：资料读取和存储统计读取核对生产 `INFO` Controller/服务链事件，非法资料更新和非法密码修改核对 User DTO/Controller `WARN` 与 HTTP 完成事件，全部验证响应同一 instance、固定 operation 与四个空所有权字段。完整构建、Python 语法检查、聚焦 GoogleTest 34/34、用户 API 集成 3/3、真实 HTTP safety 集成 1/1（脚本本次实际 606 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1410 项，1403 通过、7 项环境门控跳过、0 失败，总耗时 489.46 秒。分享、管理员、认证等其他历史请求域和共享基础设施边界，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.22 认证日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定认证请求的类型化日志、分类和脱敏边界。HTTP 指标只将精确的 `/api/auth/register`、`/api/auth/login`、`/api/auth/refresh` 和 `/api/auth/logout` 归一为低基数 `operation=auth`；认证根路径、尾随斜杠、额外后缀、未知认证路由和相似路径保持 `other`。公开 REST 路由、请求体、响应信封、错误码及访问令牌和刷新令牌生命周期语义没有变化。

`AuthController` 在 DTO 解析、认证属性读取或 Authorization 头处理前从请求属性创建显式 `LogContext`，按值传过认证 DTO、`AuthService` 的注册/登录/刷新/登出协程及用户查询、登录状态更新和失败计数辅助协程。认证请求只填充真实 `request_id`、`instance_id` 和固定 operation，`upload_id`、`job_id`、`lease_owner`、`state_version` 保持 JSON `null`；密码、密码哈希、Authorization 头、access token 和 refresh token 均不进入日志。共享 `DtoBase`、`TokenService`、`RedisService` 和过滤器未接收显式调用方上下文时继续保持空上下文，不从线程、令牌、用户或消息文本推断所有权字段；构造期服务初始化日志仍是无 HTTP 上下文的进程事件。

`MetricsServiceTest` 锁定四个精确 path 与五个负边界，`AuthLogContextContractTest` 锁定 Controller、认证 DTO、四个公开服务协程、三个辅助协程的显式上下文传播和敏感值禁记。`SafetyUploadInvariantsIntegration` 使用调用方 request ID 覆盖重复注册失败、登录成功、刷新成功和登出成功，逐行核对响应、Controller/DTO/服务及 HTTP 完成 NDJSON 的 request/instance、固定 operation 和四个空所有权字段，并扫描受管后端 stdout 确认本轮密码及两组 access/refresh token 均未泄漏。完整构建、Python 语法检查、聚焦 GoogleTest 53/53、认证流程集成 3/3、真实 HTTP safety 集成 1/1（脚本本次实际 634 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1411 项，1404 通过、7 项环境门控跳过、0 失败，总耗时 472.66 秒。分享、管理员等其他历史请求域和共享基础设施边界，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.23 分享日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定分享请求的类型化日志、分类、审计和脱敏边界。HTTP 指标将 `/api/share`、单段详情、cancel 兼容入口以及带单个非空分享码段的 access/browse/save 注册形状归一为低基数 `operation=share`；分享下载信息和内容的精确注册形状继续使用 `operation=download`。尾随斜杠、额外路径段、缺失下载文件段和相似前缀保持 `other`，公开 REST 路由、鉴权、请求/响应信封、错误码、限流及分享令牌生命周期语义没有变化。

`ShareController` 在 DTO 或请求属性处理前从请求追踪属性创建显式 `LogContext`，按值传过全部九个 Share DTO 解析入口、`ShareService` 的请求协程及所有权、查询、状态、计数和失败访问辅助协程。创建、访问、口令失败、取消和下载审计 details 保存同一 request/operation，审计 fail-open 错误也保留调用方上下文；分享请求不冒充 upload/job/lease/version 所有权。原始 Share Token、`X-Share-Token`/owner Authorization 值、分享密码和密码哈希均不进入应用日志或审计 details；共享 DtoBase、ContentService、RedisService、缓存、事务运行器和过滤器未接收显式上下文时不从线程、凭据或领域 ID 推断关联，构造期 ShareService 日志继续作为无 HTTP 上下文的进程事件。

`MetricsServiceTest` 锁定分享与下载的精确形状及负边界，`ShareLogContextContractTest` 锁定 Controller、DTO、服务辅助协程、审计和敏感值禁记，`ShareAuditServiceTest` 锁定五类审计的关联字段及空上下文 null 语义。`SafetyUploadInvariantsIntegration` 使用不同调用方 request ID 覆盖创建/列表校验失败、缺失详情、缺失批量取消和缺失公开访问，逐行核对响应、Controller/DTO/服务及 HTTP 完成 NDJSON，并查询分享审计和扫描受管日志确认本轮密码与 owner JWT 未泄漏。完整构建、Python 语法检查、分享 GoogleTest 270/270、分享集成 6/6、真实 HTTP safety 集成 1/1（脚本本次实际 693 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1413 项，1406 通过、7 项环境门控跳过、0 失败，总耗时 481.95 秒。管理员等其他历史请求域、共享基础设施边界和目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.24 核心管理员日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定核心管理员请求的类型化日志与审计边界。`AdminController` 的 13 个核心业务处理器在 DTO 解析或服务调用前创建固定 `operation=admin` 的显式 `LogContext`，按值传过 6 个直接记录日志的 Admin DTO 和 `AdminService` 的 13 个公开请求协程；用户、分享、配额和分页标识不冒充 `upload_id`、`job_id`、`lease_owner` 或 `state_version`，四个非管理员所有权字段保持 JSON `null`。手动过期清理继续使用已验收的 `operation=cleanup`，存储任务与恢复管理 Controller 保持独立后续边界，未被本批泛化为核心管理员链路。

八类管理员操作审计现在使用 JSON 序列化器写入结构化 details，并保存调用方同一 `request_id` 与 `operation`；全局存储统计、分享列表和分享详情等读取审计使用已认证管理员的真实 `user_id`，不再写入无效的操作人 0。原本长 33 字符而无法写入 `operation_logs.action VARCHAR(32)` 的可用空间动作已收敛为 `admin.user.available_space_set`，无需数据库迁移。审计仍不记录 Authorization、JWT、密码、分享令牌或密码哈希。真实分享查询同时暴露两处既有 PostgreSQL 语法错误，相关状态刷新已由非法的 `UPDATE shares s SET s.status` 修正为 PostgreSQL 支持的 `UPDATE shares AS s SET status`，列表和详情路径均由源码合同锁定。

`AdminLogContextContractTest` 锁定 13 个 Controller、6 个 DTO、13 个服务协程、八个审计调用点、结构化序列化、真实操作人和 PostgreSQL alias 语法；`SafetyUploadInvariantsIntegration` 使用四个不同调用方 request ID 覆盖非法用户列表状态、管理员修改自身状态、缺失分享详情和成功全局存储统计，逐行核对 DTO/Controller/服务/HTTP NDJSON，并直接查询 `operation_logs` 验证真实管理员 ID、request/operation 关联及敏感值禁记。完整构建、Python 语法检查、管理员 GoogleTest 59/59、真实 HTTP safety 集成 1/1（脚本本次实际 725 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1414 项，1407 通过、7 项环境门控跳过、0 失败，总耗时 487.73 秒。存储任务/恢复管理请求、共享基础设施边界和目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.25 存储任务管理日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定独立的存储任务管理关联边界。`StorageJobAdminController` 的列表、详情和死信重放在解析前创建固定 `operation=admin` 的显式 `LogContext`；列表没有单任务所有权并保持 `job_id=null`，详情与重放只在正整数路径 ID 校验成功后写入真实 `storage_jobs.id`。上下文按值传入 `StorageJobAdminService` 的三个请求协程，既有错误与重放成功事件不再丢失 request/instance/operation/job；upload ID、lease owner 和 state version 保持 JSON `null`，非法路径文本、aggregate ID、dedupe key、payload 和 message 不作为关联推断来源。现有 HTTP 指标分类保持不变，没有引入新的高基数或重复 operation。

确认重放事务现在通过 JSON 序列化器在 `operation_logs.details` 保存调用方同一 `request_id` 与 `operation=admin`，同时保留真实任务 ID、原状态、原尝试次数和规范化原因；dry-run 与冲突仍不写审计，日志和审计不记录管理员 JWT、Authorization、对象存储凭据或任务 payload。上传诊断内部使用的 `ListRelatedToUpload` 不属于存储任务管理 HTTP 链路，本批没有从 upload ID、staging prefix 或返回任务反推上下文；存储恢复管理 Controller 继续作为下一独立边界。

`StorageJobAdminLogContextContractTest` 锁定三个 Controller、三个服务请求协程、单任务 ID 建立点、事务传播、结构化审计和敏感值禁记；现有 StorageJobAdmin DTO 8 项继续覆盖状态/类型/分页、正整数 ID、dry-run 和确认合同。`StorageJobOperationsIntegration` 为列表、详情、dry-run、确认重放和重复冲突分别发送唯一 request ID，逐行核对受管 `ops-api` NDJSON 的 request/instance/admin 与类型化 job 空值/实值，并直接查询 PostgreSQL 确认审计只写一次且保存关联二元组。完整构建、Python 语法检查、存储任务管理 GoogleTest 9/9、真实 HTTP StorageJobOperations 集成 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1415 项，1408 通过、7 项环境门控跳过、0 失败，总耗时 474.54 秒。存储恢复管理请求、共享基础设施边界和目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.26 存储恢复管理日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定租约释放、清理重建与存储对账三类恢复管理命令的类型化关联边界。`StorageRecoveryAdminController` 的三条路由在解析前创建固定 `operation=admin` 的显式 `LogContext`；上传命令只在 DTO 完整校验后补入规范 upload ID，对账命令不把 scan ID、scope、dedupe key、page size 或 cursor 冒充为既有类型化字段。上下文按值传入 `StorageRecoveryAdminService`，服务只以 PostgreSQL 实际返回的 upload state version、lease owner 和持久任务主键补充观测值；调用方提交的 `expected_state_version`、`expected_lease_owner` 与确认值只用于 CAS/安全确认，校验失败和冲突事件的未知字段继续为 JSON `null`。

三个确认执行事务均在既有 `operation_logs.details` 中保存调用方同一 `request_id` 与 `operation=admin`，同时保留规范化原因和既有业务详情；dry-run 与冲突仍不写审计。日志和审计不记录 Authorization、管理员 JWT、对象存储凭据、对象 key/prefix、任意 cursor 或任务 payload。既有 MetricsService 对 `/api/admin/*` 的低基数分类保持不变；共享 `DtoBase`、`TransactionRunner` 等基础设施未从线程、断言或领域文本反推恢复上下文。

`StorageRecoveryAdminLogContextContractTest` 锁定三个 Controller、三个服务协程、实际响应字段来源、事务审计传播、禁止断言值推断及敏感值禁记；现有恢复 DTO 7 项继续覆盖 dry-run、精确确认、固定 scope 和稳定响应。`StorageJobOperationsIntegration` 为租约 dry-run/陈旧 owner 冲突/确认释放/重复冲突、cleanup 创建/重启/DeadLetter，以及四个固定对账 scope 的 dry-run/入队/重复冲突分别发送唯一 request ID，逐行核对受管 `ops-api` NDJSON，并直接查询 PostgreSQL 审计关联二元组。完整构建、Python 语法检查、存储恢复管理 GoogleTest 8/8、真实 HTTP StorageJobOperations 集成 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1416 项，1409 通过、7 项环境门控跳过、0 失败，总耗时 472.80 秒。共享基础设施边界和目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.27 共享 DTO 纯验证记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定共享 DTO 的无日志副作用合同。`DtoBase` 的无效 JSON、必填/可选标量、正整数路径/查询值和 ID 数组共 14 类 helper 继续返回既有错误码、规范化消息与解析结果，不接收或推断 `LogContext`；原有 31 处无上下文 `Logger::Warn()` 和 `LogHelper.hpp` 依赖已删除。`RequireJsonBody` 使用与 Drogon 相同的 `collectComments=false` 和运行时 stack limit 静默解析 JsonCpp，并以调用栈内受所有权保护的只读 JSON 返回结果，避免框架 `getJsonObject()` 在语法错误时额外产生 `request_id=null` 的 `LOG_DEBUG`。仍直接记录领域事件的具体 DTO 改为显式包含 `LogHelper.hpp`，不再通过共享基类取得日志声明；公开 REST 请求、响应信封、错误码和合法 JSON 语义没有变化。

新增 `DtoBaseTest.ValidationErrorsArePureResults` 覆盖全部 14 类 helper 的错误码与精确消息，源码合同禁止 `Logger::`/`LogHelper.hpp` 回归，并将应用与 Drogon 框架日志同时接入内存 sink 验证无效输入产生零事件。现有 DTO 相关 GoogleTest 22/22 回归通过。`SafetyUploadInvariantsIntegration` 以唯一 `X-Request-Id` 发送空 `trash_ids` 恢复请求，确认 Trash DTO/Controller/HTTP 边界仍记录同一 request/实际 instance 的完整失败事件，同时受管 stdout 不存在共享 helper 过去写出的同消息空 request 重复事件。

完整构建、Python 语法检查、共享 DTO GoogleTest 1/1、DTO 相关 GoogleTest 22/22、真实 HTTP safety 集成 1/1（脚本本次实际 724 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1417 项，1410 通过、7 项环境门控跳过、0 失败，总耗时 470.38 秒。具体 DTO 的既有直接日志、`TransactionRunner`、Redis、认证/限流过滤器等其他共享基础设施边界，以及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.28 事务边界日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定共享事务运行器的显式关联合同。`TransactionRunner` 现在按值持有调用方提供的 `LogContext`，数据库/普通异常、提交 callback 失败、持久额外 owner 拒绝提交和 rollback 异常全部使用该上下文；无请求调用继续通过默认空上下文输出 JSON `null`，不读取 thread-local，也不从 SQL、异常正文、事务对象或 owner 计数反推字段。callback 领域错误原样返回、异常映射默认错误、rollback 失败不覆盖原结果和等待提交 callback 的既有语义均未变化。

文件 move/copy、上传 init/complete/cancel/expire、移入回收站、存储任务重放和三类存储恢复管理共 11 个托管事务入口，以及 folder rename、share create/save、永久删除和 Blob GC 共 5 个手动提交入口，均显式传入已有请求或 Worker 上下文。上传最终事务取得租约并在事务前续租后建立运行器，因此真实 insert 异常事件使用与回滚后持久任务一致的 upload ID、lease owner 和 state version；非上传请求继续保持其四个所有权字段为空。

新增 `TransactionRunnerLogTest.FailureEventsPreserveExplicitContextAndNullDefaults`，以内存 NDJSON sink 同时核对数据库异常、普通异常、rollback、提交失败、额外 owner 与默认空上下文；缓存顺序合同继续验证带上下文的提交先于代际失效。`SafetyUploadInvariantsIntegration` 以唯一调用方 request ID 注入真实 PostgreSQL files insert 异常，并将事务事件与响应 instance、upload task 的 lease owner/state version 对账，同时拒绝包含唯一故障标记的空 request 应用事件。完整构建、Python 语法检查、事务/缓存聚焦 GoogleTest 12/12、真实 HTTP safety 集成 1/1（731 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；首次完整 CTest 的既有共享用户预留总量竞态偶发偏差 45 字节，随后 safety 聚焦复跑 731/731 和第二次完整 CTest 均通过，最终完整结果为 1418 项中 1411 通过、7 项环境门控跳过、0 失败，总耗时 474.32 秒。Redis、认证/限流过滤器等其他共享基础设施边界及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.29 文件 DTO 日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定文件 DTO 直接日志的显式关联合同。`FileDto.hpp` 中 init、complete、列表、下载信息、下载、rename、move、copy、delete 和 search 共 10 个解析入口现在按值接收调用方 `LogContext`，原有 23 条 `DEBUG` 与 24 条 `WARN` 事件全部使用该上下文；无上下文的直接调用继续通过默认参数输出 JSON `null`，DTO 不从请求字段、路径 ID 或消息文本推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`FileController` 的 11 个调用点均传入请求级上下文，complete 仍只在 DTO 完整校验成功后由 Controller 补入规范 upload ID，公开请求、响应、校验结果和错误码没有变化。

新增 `FileDtoLogContextTest` 源码合同与内存结构化日志用例，锁定 10 个入口、47 条直接日志、11 个 Controller 调用点、六个关联字段的按值保留、默认空上下文和禁止 DTO 推断所有权字段。`SafetyUploadInvariantsIntegration` 使用唯一调用方 request ID 发送真实非法文件哈希 init 请求，确认响应回显、DTO 警告的 request/instance/operation 一致、upload 仍为空，并拒绝同消息的空 request 应用重复事件。完整构建、Python 语法检查、文件 DTO 聚焦 GoogleTest 78/78、真实 HTTP safety 集成 1/1（733 项断言）和 OpenSpec 严格校验 24/24 通过；首次完整 CTest 中既有分区恢复 readiness 时序断言瞬态返回 503，失败目标复跑 733/733 后通过，第二次完整 CTest 共 1420 项，1413 通过、7 项环境门控跳过、0 失败，总耗时 477.86 秒。Redis、认证/限流过滤器等其他共享基础设施边界及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.30 认证过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定认证与授权过滤器的显式关联合同。新增的 `GetFilterLogContext` 只从请求属性读取既有 request ID，并通过 `ClassifyHttpOperation` 与 `HttpOperationName` 取得低基数 operation；请求属性缺少 request ID 时保持 JSON `null`，但仍保留按实际 path 分类的 operation。该边界不读取 Authorization、Share Token、用户、分享或令牌标识，也不推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。

`JwtAuthFilter` 的 8 条、`ShareAuthFilter` 的 4 条和 `AdminAuthFilter` 的 3 条直接事件现在全部使用同一个按请求创建的 `LogContext`。公开路径豁免、缺失/畸形/过期/撤销令牌、Redis 撤销检查失败、分享权限范围拒绝、管理员角色/状态拒绝及成功放行的鉴权顺序、响应信封和错误码均未改变；密码、Authorization、JWT、Share Token 和文件正文仍禁止进入结构化字段或消息。身份和令牌值只保留在既有领域消息中，不冒充类型化所有权字段。

新增 `AuthFilterLogContextContractTest` 与内存 NDJSON 用例，锁定共享构造器、15 个调用点、各级别事件数量、缺少 request ID 的空值语义，以及 JWT、分享令牌和管理员三类拒绝事件的 request/instance/operation 与四个空所有权字段。`SafetyUploadInvariantsIntegration` 以三个不同调用方 request ID 真实触发缺少 JWT、缺少 Share Token 和普通用户访问管理员接口，并扫描受管日志确认临时密码及 access/refresh token 未泄漏；临时普通用户和 refresh-token 状态在 finally 中清理，测试后数据库残留数为 0。完整构建、Python 语法检查、认证过滤器聚焦 GoogleTest 96/96、真实 HTTP safety 集成 1/1（752 项断言）、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1422 项，1415 通过、7 项环境门控跳过、0 失败，总耗时 477.52 秒。`TokenService`、`RedisService`、认证/分享/管理员限流过滤器等其他共享基础设施边界及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.31 分享限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定分享 access 与认证后操作限流器的显式关联合同。两个 `doFilter` 入口现在各通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；access 计数失败/超限、browse/download/save 的 JTI 属性缺失/空值、计数失败和超限共 6 条直接事件全部使用该上下文。限流键、窗口、阈值、认证顺序、Redis 故障 fail-open 和 429 响应未改变。

日志的类型化 operation 继续表示 HTTP 路由而不是内部限流桶：access、browse 和 save 为 `share`，分享下载为 `download`；`/api/share/save/{share_code}` 即使消耗 download 桶，也不被误标为下载请求。日志不写入原始 Authorization、Share Token 或 JTI，也不从客户端 IP、JTI、分享码、计数键或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`；缺少 request 属性的直接调用保持 request JSON `null` 与有界 operation。

新增 `ShareRateLimitLogContextContractTest` 与内存 NDJSON 用例，锁定 2 个上下文构造点、4 条 `ERROR`、2 条 `WARN` 以及全部失败/拒绝分支的 request/instance/operation、空所有权字段和凭据排除。`test_share_rate_limit.py` 为 access、browse 和 download 的真实最终 429 请求分别注入唯一 request ID，从该脚本独占的 API stdout NDJSON 对账响应实例、有界 operation 和四个空所有权字段，并继续扫描 Redis 键、日志、审计与保存 evidence 的凭据/JTI 排除。完整构建、Python 语法检查、分享限流聚焦 GoogleTest 40/40、直接集成 10/10、注册分享限流 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1424 项，1417 通过、7 项环境门控跳过、0 失败，各测试计时合计 472.12 秒。`TokenService`、`RedisService`、其余用户/IP 限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.32 注册限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定全局注册 IP 限流器的显式关联合同。`RegisterRateLimitFilter` 现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；Redis 计数失败、成功检查和超限拒绝三条直接事件分别以原有 `ERROR`、`DEBUG` 和 `WARN` 级别使用该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍取得真实 `RedisService` 并调用既有固定窗口 helper；精确 `/api/auth/register` 范围、规范化 IP 键、配置回退、Redis 故障 fail-open 和 429 响应未改变。

三条事件统一保留调用方 request、实际 instance 和有界 `auth` operation；缺少 request 属性的直接调用继续输出 JSON `null`。过滤器不读取注册正文或凭据，不从客户端 IP、计数键、窗口、阈值或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`RegisterRateLimitLogContextContractTest` 与内存 NDJSON 用例锁定一个上下文构造点、三个日志级别、失败/成功/拒绝分支、空所有权字段、默认真实 Redis helper 以及原始注册密码排除；`FilterOwnershipTest` 同步锁定注入计数器失败后的放行顺序。

`SafetyUploadInvariantsIntegration` 读取当前配置，在窗口切换边界外以 TTL 将本机规范化 IP 的当前固定窗口键预置到阈值，再以唯一 request ID 发送真实注册请求；它核对 HTTP 429、业务码 `10005`、四个限流头、响应 request/instance、warning 的 `auth` 关联和四个空所有权字段，并在 finally 中清理注册限流键族及扫描两个注册密码未进入受管日志。完整构建、Python 语法检查、注册限流与过滤器归属聚焦 GoogleTest 23/23、直接 safety 集成 763/763、注册 safety CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1426 项，1419 通过、7 项环境门控跳过、0 失败，总耗时 475.65 秒。`TokenService`、`RedisService`、其余用户/IP 限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.33 上传限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定路由级上传限流器的显式关联合同。`UploadRateLimitFilter` 现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；缺失请求属性、Redis 计数失败、成功检查和超限拒绝四条直接事件继续使用原有 `WARN`、`ERROR`、`DEBUG` 和 `WARN` 级别，并统一保留该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍初始化真实 `RedisService` 并调用既有固定窗口 helper；路由归属、用户固定窗口键、配置回退、Redis 故障 fail-open、阈值和 429 响应未改变。

init、chunk、complete 和 cancel 继续共享同一个用户限流桶，但类型化 operation 分别保持 `upload_init`、`upload_chunk`、`upload_complete` 和 `upload_cancel`，不会因共享计数键而合并语义。过滤器不读取 Authorization 或请求正文，也不从用户、path、计数键、窗口、阈值或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`UploadRateLimitLogContextContractTest` 与内存 NDJSON 用例锁定一个上下文构造点、四个日志调用、失败/成功/拒绝分支、四类 operation、空所有权字段、默认真实 Redis helper 及凭据/正文排除；`FilterOwnershipTest` 同步锁定注入计数器失败后的放行顺序。

`test_upload_rate_limit.py` 读取活动配置，以脚本独占的受管 API 连续发送阈值数量的真实 init 请求，再为最终 429 注入唯一 request ID；它核对业务码 `10005`、四个限流头、响应 request/instance、warning 的 `upload_init` 关联和四个空所有权字段，并扫描受管 stdout 确认登录密码、JWT 和最终请求文件名未进入日志。完整构建、Python 语法检查、上传限流与过滤器归属聚焦 GoogleTest 13/13、直接上传限流集成 8/8、上传限流注册 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1428 项，1421 通过、7 项环境门控跳过、0 失败，总耗时 481.65 秒。`TokenService`、`RedisService`、下载/文件夹/管理员/通用用户限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.34 所有者下载限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定路由级所有者下载限流器的显式关联合同。`DownloadRateLimitFilter` 现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；Redis 计数失败、成功检查和超限拒绝三条直接事件继续使用原有 `ERROR`、`DEBUG` 和 `WARN` 级别，并统一保留该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍取得真实 `RedisService` 并调用既有固定窗口 helper；下载信息/内容路由归属、用户固定窗口键、配置回退、Redis 故障 fail-open、阈值和 429 响应未改变。

下载信息与内容路由继续共享同一个所有者用户桶，两者类型化 operation 均保持 `download`；该桶不与使用分享 JTI 的公开下载限流桶混用。过滤器不读取 Authorization、Range 或请求正文，也不从用户、path、file ID、计数键、窗口、阈值或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`DownloadRateLimitLogContextContractTest` 与内存 NDJSON 用例锁定一个上下文构造点、三个日志调用、失败/成功/拒绝分支、两类路由、空所有权字段、默认真实 Redis helper 及凭据/Range/正文排除；`FilterOwnershipTest` 同步锁定注入计数器失败后的放行顺序。

`test_download_flow.py` 读取活动配置和当前已认证用户，以 TTL 将该用户的下载固定窗口键预置到阈值，再以唯一 request ID 和 Range 发送真实内容下载请求；它核对 429、业务码 `10005`、四个限流头、响应 request/instance、warning 的 `download` 关联和四个空所有权字段，并验证 `download_count/last_accessed_at` 未改变以及受管 stdout 不含登录密码、JWT 或 Range。完整构建、Python 语法检查、下载限流与过滤器归属聚焦 GoogleTest 22/22、直接下载流集成 122/122、下载流注册 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1430 项，1423 通过、7 项环境门控跳过、0 失败，总耗时 476.13 秒。`TokenService`、`RedisService`、文件夹/管理员/通用用户限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.35 文件夹限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定路由级文件夹限流器的显式关联合同。`FolderRateLimitFilter` 现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；Redis 计数失败、成功检查和超限拒绝三条直接事件继续使用原有 `ERROR`、`DEBUG` 和 `WARN` 级别，并统一保留该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍取得真实 `RedisService` 并调用既有固定窗口 helper；`/api/folder/` 路由范围、用户固定窗口键、配置回退、Redis 故障 fail-open、阈值和 429 响应未改变。

tree 与 breadcrumb 路由继续共享同一个用户限流桶并保持 `folder_query` operation，create 与 rename 路由使用同一桶但保持 `folder_mutation` operation，不因共享计数键而合并语义。过滤器不读取 Authorization 或请求正文，也不从用户、path、folder ID、计数键、窗口、阈值或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`FolderRateLimitLogContextContractTest` 与内存 NDJSON 用例锁定一个上下文构造点、三个日志调用、失败/成功/拒绝分支、四类路由、空所有权字段、默认真实 Redis helper 及凭据/目录名排除；`FilterOwnershipTest` 同步锁定注入计数器失败后的放行顺序。

`test_folder_lifecycle.py` 读取活动配置和当前已认证用户，以 TTL 将该用户的文件夹固定窗口键预置到阈值，再以唯一 request ID 和唯一目录名发送真实创建请求；它核对 429、业务码 `10005`、四个限流头、响应 request/instance、warning 的 `folder_mutation` 关联和四个空所有权字段，并验证被拒绝的目录未创建且受管 stdout 不含登录密码、JWT 或目录名。完整构建、Python 语法检查、文件夹限流与过滤器归属聚焦 GoogleTest 22/22、直接文件夹生命周期集成 25/25、文件夹生命周期注册 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1432 项，1425 通过、7 项环境门控跳过、0 失败，总耗时 479.10 秒。`TokenService`、`RedisService`、管理员/通用用户限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.36 管理员限流过滤器日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定路由级管理员限流器的显式关联合同。`AdminRateLimitFilter` 现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；Redis 计数失败、成功检查和超限拒绝三条直接事件继续使用原有 `ERROR`、`DEBUG` 和 `WARN` 级别，并统一保留该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍取得真实 `RedisService` 并调用既有固定窗口 helper；管理员认证顺序、`/api/admin/` 路由范围、用户固定窗口键、配置回退、Redis 故障 fail-open、阈值和 429 响应未改变。

普通管理员、上传诊断、存储任务与恢复命令继续共享同一个管理员用户桶并保持 `admin` operation，精确过期清理路由使用同一桶但保持 `cleanup` operation，不因共享计数键而合并语义。过滤器不读取 Authorization 或请求正文，也不从用户、path、upload/job/scan ID、计数键、窗口、阈值或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`AdminRateLimitLogContextContractTest` 与内存 NDJSON 用例锁定一个上下文构造点、三个日志调用、失败/成功/拒绝分支、五类代表路由、空所有权字段、默认真实 Redis helper 及凭据/恢复正文排除；`FilterOwnershipTest` 同步锁定当前 21 条路由的认证先行归属与注入计数器失败后的放行顺序。

`test_storage_job_operations.py` 读取活动配置和当前已认证管理员，以 TTL 将该用户的管理员固定窗口键预置到阈值，再以唯一 request ID 发送真实存储任务列表请求；它核对 429、业务码 `10005`、四个限流头、响应 request/instance、warning 的 `admin` 关联和四个空所有权字段，并验证 `storage_jobs` 与 `operation_logs` 行数未改变且受管 stdout 不含管理员 JWT。完整构建、Python 语法检查、管理员限流与过滤器归属聚焦 GoogleTest 21/21、直接存储任务运维集成 1/1、存储任务运维注册 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1434 项，1427 通过、7 项环境门控跳过、0 失败，总耗时 476.79 秒。`TokenService`、`RedisService`、通用用户限流过滤器及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.37 回收站通用用户限流过滤器日志关联记录（2026-07-22）

权威需求、数据库、部署运维、系统测试、单元测试和 OpenSpec 文档先行纠正旧的全局/令牌桶描述：名为 `RateLimitFilter` 的过滤器实际只由 `TrashController` 的 list、restore、两条 permanent-delete 和 delete-all 共 5 条路由声明，使用 `rate:api:{user_id}:{window}`、100 次/60 秒固定窗口。它现在在入口通过 `GetFilterLogContext` 从请求属性和现有 HTTP 分类器构造一次上下文；缺失属性、Redis 计数失败、成功检查、成功耗时、超限拒绝和失败耗时 6 类直接事件继续使用原有级别，并统一保留该上下文。计数动作新增同签名可注入边界供内存测试使用，默认构造仍初始化真实 `RedisService` 并调用既有固定窗口 helper；JWT 认证先行、Redis 故障 fail-open、阈值、路由归属、429 信封、三项 `X-RateLimit-*` 头及不返回 `Retry-After` 的响应合同未改变。

5 条回收站路由继续共享同一个用户桶并保持 `trash` operation。过滤器不读取 Authorization 或请求正文，也不从用户、path、trash ID、计数键、窗口、阈值、耗时或依赖结果推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`RateLimitLogContextTest` 与内存 NDJSON 用例锁定一个上下文构造点、六个日志调用、缺失属性/失败/成功/拒绝分支、空所有权字段、默认真实 Redis helper/初始化、三项限流头和凭据/正文/trash ID 排除；`FilterOwnershipTest` 同步锁定当前 5 条路由的唯一归属与注入计数器失败后的放行顺序。

`test_trash_lifecycle.py` 创建并软删除唯一探针文件，以 TTL 将当前已认证用户的固定窗口键预置到 100，再以唯一 request ID 发送真实 delete-all 请求；它核对 429、业务码 `10005`、三项限流头、`Retry-After` 缺失以及 warning/失败耗时事件的 request/instance/`trash` 和四个空所有权字段，并在清理限流键后证明探针仍在回收站，随后由原有 delete-all 流程回收。完整构建、Python 语法检查、回收站通用用户限流与过滤器归属聚焦 GoogleTest 13/13、直接回收站生命周期集成 43/43、回收站生命周期注册 CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1436 项，1429 通过、7 项环境门控跳过、0 失败，总耗时 479.90 秒。`TokenService`、`RedisService` 共享基础设施边界及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.38 TokenService 日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定令牌服务的显式所有权边界。`TokenService` 的 access/refresh/share 生成、校验、轮换和撤销共 8 个请求可达入口新增可选 `LogContext`，`AuthService`、`ShareService`、`JwtAuthFilter` 和 `ShareAuthFilter` 从已有请求边界按值传入同一上下文；CPU 池清理回调也按值保留它。21 条请求事件统一携带真实 request/instance/operation，且不从 JWT claims、用户、share code、JTI、Redis 键或线程推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。单例初始化、CPU 池创建、两个定时器启动、周期池指标和缓存淘汰共 6 条事件继续是无请求上下文的进程事件；JWT claims、TTL、Redis CAS/撤销语义和 API 响应不变，同时删除未使用的 Redis client 字段和 TTL helper。

新增 `TokenServiceLogContextContractTest` 与 3 个内存 NDJSON 用例，锁定 8 个入口默认值、21/6 条请求/进程日志归属、全部生产调用点、access/refresh/share 成功和畸形输入、默认空调用方语义，以及密钥、原始 access/refresh/share token 不进入结构化日志。认证和分享源码合同同步锁定调用方上下文；`SafetyUploadInvariantsIntegration` 真实触发登录、刷新、登出、畸形 owner JWT 和畸形 visitor share token，核对 CPU 池跨线程计时与解析失败事件仍匹配响应的 request/instance/operation，并扫描受管日志排除临时密码、Authorization 和原始 token。

完整构建、Python 语法检查、TokenService/认证/分享/过滤器聚焦 GoogleTest 124/124、直接 safety 集成 767/767、注册 safety CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1440 项，1433 通过、7 项环境门控跳过、0 失败，总耗时 491.76 秒。`RedisService` 共享基础设施边界及目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.39 RedisService 日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 Redis 命令边界的显式关联合同。`RedisService` 的 PING、单键、批量、计数、CAS 和固定窗口共 13 个命令 API 新增可选 `LogContext`；23 条命令成功、协议、解析和依赖失败事件统一使用调用方 request/instance/operation，单例初始化继续是空请求关联的进程事件。日志只保留固定命令名及数量、TTL、布尔结果等有界诊断，不记录 key、value、expected/new value、Lua、异常文本、token hash/JTI、分享码、IP、文件列表内容、endpoint 或凭据，也不从命令输入推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`；Redis 命令、TTL、事务、Lua/CAS、依赖指标、`Result` 错误及上层 fail-open/fail-closed 语义不变。

已有请求上下文现按值穿过 `TokenService` 的 refresh/access/share Redis 入口、`AuthService` 登录计数与清理、`ShareService` 口令失败计数、`FileListCache`/`FileQueryService`、七类限流计数适配器、管理员系统状态和 readiness；文件列表失效调用点同步传入各自请求上下文，健康控制器为 live/ready 建立固定 `health` operation。新增 `RedisServiceLogContextContractTest` 和 3 个真实 Redis/内存 NDJSON 用例，锁定 13 个默认入口、23 条命令事件、主要生产调用点、7 类成功日志、命令失败脱敏及默认空调用方语义。`SafetyUploadInvariantsIntegration` 将真实登录限流键预置为唯一非整数值，确认 Redis Lua 错误保持登录 fail-open、错误事件与响应使用同一关联，并扫描受管日志排除探针 key/value、密码和 token。

完整构建、Python 语法检查、Redis/Token/缓存/健康/限流聚焦 GoogleTest 52/52、直接 safety 集成 769/769、注册 safety CTest 1/1、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过。首次完整 CTest 的唯一失败是 `FileMutationServiceMoveContractTest` 仍断言旧的无上下文缓存失效调用文本；更新为显式 `log_context` 合同后聚焦复验 6/6 通过，第二次完整 CTest 共 1444 项，1437 通过、7 项环境门控跳过、0 失败，总耗时 476.32 秒。目标 MinIO/云 S3 和多实例环境门控仍未执行，因此 12.1 的两个总任务继续保持未勾选。

### 12.40 操作日志查询关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定操作日志查询的显式关联合同。HTTP 分类器现只把精确 `/api/logs` 映射为低基数 `operation=operation_log`，尾斜杠、子路径和相似路径继续归入 `other`；`OperationLogController` 在解析用户与分页参数前从请求属性创建一次 `LogContext`，并按值传入 `OperationLogService::GetList`。服务的非 HTTP 调用保留默认空上下文，单例构造事件继续是无请求归属的进程事件。

Controller 接收与失败事件、Service 的查询与计数数据库失败事件均使用同一 request/instance/operation；诊断日志不再写 peer IP、用户 ID、页码、审计详情、SQL、连接信息、异常正文或业务错误正文，也不从查询参数或审计行推断 `upload_id`、`job_id`、`lease_owner` 和 `state_version`。`MetricsServiceTest` 锁定精确分类边界，`OperationLogQueryContractTest` 锁定上下文传播、默认空调用方和脱敏边界；`SafetyUploadInvariantsIntegration` 以两个唯一 request ID 分别执行认证成功查询与未认证拒绝，核对响应 request/instance、Controller 或 HTTP 完成事件的固定 operation，以及四个所有权字段均为 JSON `null`。

完整构建、Python 语法检查、OperationLog 分类/服务聚焦 GoogleTest 10/10、直接 safety 集成 777/777、注册 OperationLog/safety CTest 10/10、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1445 项，1438 通过、7 项环境门控跳过、0 失败，总耗时 473.12 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.41 上传诊断日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定管理员上传诊断的显式关联合同。`UploadDiagnosticController` 现在在 DTO 解析前从请求属性建立固定 `operation=admin` 的 `LogContext`，原始路径或分页校验失败时不写类型化 upload ID；只有 DTO 返回规范非空 ID 后，Controller 才填入 `upload_id`、记录固定接收事件并按值调用 `UploadDiagnosticService`。

诊断服务仅在调用方 upload ID 与 PostgreSQL 返回任务行精确一致时，补入该行真实 `state_version` 和可选 `lease_owner`；同一上下文逐个传入 staging `HeadChunkObject` 与 `StorageJobAdminService::ListRelatedToUpload`。关联任务可能为零或多行，整条诊断链保持 `job_id=null`；helper 和默认空调用不从 upload/prefix、对象描述符、任务行、返回任务或消息推断字段。Controller 接收、Service 完成及两个异常事件均使用固定消息，不记录路径/分页、用户/文件元数据、hash、对象 key/prefix、ETag、任务 payload/error、SQL、连接信息、异常正文、Authorization/JWT 或存储凭据；数据库查询、对象 HEAD、分页、响应和只读语义不变。

新增上传诊断源码合同并扩展真实存储任务运维集成：唯一 request ID 的 Controller 事件携带 validated upload 且 state/lease 为空，Service 完成事件再携带数据库真实 `state_version=3` 与 lease owner，两者 request/实际 instance/admin/upload 一致、job ID 为空且 message 精确等于固定文本。完整构建、Python 语法检查、聚焦 GoogleTest 6/6、直接存储任务运维集成 1/1、注册上传诊断/存储任务 CTest 8/8、拓扑合同 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1446 项，1439 通过、7 项环境门控跳过、0 失败，总耗时 476.94 秒。真实 S3/MinIO HEAD 和多实例环境门控仍未执行，其他共享数据库 helper 也尚未全部关联，因此 12.1 的两个总任务继续保持未勾选。

### 12.42 配额服务日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定共享配额边界的显式关联合同。`QuotaService` 的独立客户端与事务客户端当前共 11 个活跃写入入口，均按值接收可选 `LogContext`，内部重载逐层原样转发；预留、释放、预留转已用、直接消费和已用量调整的 17 条操作事件统一保留调用方已有的 request/instance/operation/upload/job/lease/version。最初纳入的两个对账查询入口及其独占事件随后按 15.62 清理。`UploadLifecycleService` 的 8 个、`FileMutationService` 的 4 个以及 `TrashService` 的 1 个生产调用点均传入原上下文；构造事件继续是无请求归属的进程事件。

配额日志只使用固定事件消息，不记录用户 ID、字节数、调整量、SQL、连接信息、业务错误正文或异常正文，也不从配额输入、影响行数、数据库结果、线程局部状态或消息文本推断类型化字段；数据库语句、事务边界、配额错误码和响应语义未改变。同步删除 `FileMutationService` 的两个以及 `TrashService`、`CleanupService` 各一个从未调用的私有配额转发器，避免保留绕过显式上下文的平行路径。`QuotaServiceContractTest` 锁定 11 个活跃入口、17 条事件、内部重载和全部生产调用点，并拒绝无调用对账外观回归；`TrashLogContextContractTest` 不再以已删除 helper 作为源码截取边界，并明确禁止该 helper 回归。

`SafetyUploadInvariantsIntegration` 临时把当前用户 quota 收紧到已用量加预留量，以唯一 request ID 发起真实非去重上传初始化；它核对 HTTP 400、业务码 `50004`、配额层与上传层两条固定 warning 的同一 `upload_init` 关联、四个所有权字段为空、数据库未创建任务或预留，并在 `finally` 中恢复且复核完整 quota。完整构建、Python 语法检查、配额/回收站日志合同聚焦 GoogleTest 6/6、相邻清理/文件变更聚焦 GoogleTest 30/30、直接 safety 集成 790/790 项断言和 OpenSpec 严格校验 24/24 通过。首次完整 CTest 的唯一失败是旧 `TrashLogContextContractTest` 仍以已删除转发器作为源码终点；同步合同后第二次完整 CTest 共 1447 项，1440 通过、7 项环境门控跳过、0 失败，总耗时 482.46 秒。`ContentService`、`FolderRepository`、`FileServiceUtils` 等其他共享数据库 helper 与目标 S3/多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.43 共享文件持久化日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定共享文件持久化边界的显式关联合同。`FolderRepository::ResolveOwnedFolderLocation` 与 `FileServiceUtils` 的 folder-location 转发、trash 批量插入、active file 批量删除、active folder 批量删除共 5 个入口现在按值接收可选 `LogContext`；`TrashService::CreateTrashRecords` 也按值转发原上下文。`UploadLifecycleService` 的 2 个、`FileMutationService` 的 4 个以及 `TrashService` 的 3 个生产调用点均保留请求已有的 request/instance/operation/所有权字段；默认非 HTTP 调用继续是空关联。

4 条直接 application 失败事件固定为 `Folder location lookup failed`、`Trash record batch insert failed`、`File batch delete failed` 和 `Folder batch delete failed`，不记录用户/文件/文件夹 ID、名称、路径、批次正文、SQL、连接信息、异常正文、Authorization/JWT 或存储凭据，也不从参数、数据库行、affected rows、错误或消息推断所有权字段。参数化 SQL、事务连接、受影响行判断、move-to-trash rollback、`Result`/整数返回值及 HTTP 500/`10006`/`Failed to delete items` 公开合同不变。

`FolderRepositoryLogContextContractTest` 锁定 5 个默认入口、Trash 转发、4 条固定事件和全部生产调用点；`test_safety_content_quota.py` 以 3 个不同唯一 request ID 和精确 PostgreSQL trigger 分别制造 trash insert、active file delete 与 active folder delete 失败，解析 `source=application` NDJSON 核对响应同一 request/实际 instance/`file_mutation`、四个空所有权字段、无空 request 重复事件且 application message 不含触发器异常正文，同时保留 active/trash/share/ref_count/quota/Blob 回滚不变量。完整构建、Python 语法检查、聚焦 CTest 6/6、直接安全网 194/194 项断言和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1448 项，1441 通过、7 项环境门控跳过、0 失败，总耗时 471.57 秒。`ContentService` 及其他尚未逐条归属的日志路径、目标 S3/多实例环境门控仍未收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.44 共享内容生命周期日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定共享内容生命周期边界的显式关联合同。`ContentService` 的两个 MD5 查找、已有 ID 批查、引用获取、单条递增、两个批量递增以及递减/Blob GC 入队共 8 个公开入口现在按值接收可选 `LogContext`，内部 reference gate 只接收显式上下文；默认单测、迁移、工具和兼容调用继续保持空关联，构造期初始化继续是无请求归属的进程事件。`UploadLifecycleService` 的 4 个、`FileMutationService` 的 4 个、`ShareService` 的 2 个和 `TrashService` 的 1 个生产调用点均原样传入已有 request/instance/operation/所有权字段。

10 条直接操作失败事件现在使用固定消息，不记录 content/file/user ID、MD5/SHA-256、size/delta、storage path、MIME、batch/affected rows、GC payload、SQL、连接信息、业务错误或异常正文、Authorization/JWT 或存储凭据，也不从输入、数据库行、错误或消息推断或覆盖类型化字段。既有参数化 SQL、行锁、ref_count 不变量、reference gate、GC 入队、事务归属、可选值/集合/`Result` 及 HTTP 响应合同不变。

`ContentServiceLogContextContractTest` 锁定 8 个默认入口、4 次内部 gate 传播、10 条固定事件和 11 个生产调用点；`test_safety_content_quota.py` 以精确 PostgreSQL `file_contents` UPDATE trigger 只破坏指定内容的引用递增，以唯一 request ID 发起真实秒传 init，核对 HTTP 500/`10006`/`Failed to update file content reference count`、响应同一 request/实际 instance/`upload_init`、四个空所有权字段、无空 request 重复事件且 application message 不含触发器异常正文，同时确认目标文件和上传任务未创建，ref_count、used/reserved quota、内容行和 Blob 不变。完整构建、Python 语法检查、聚焦 CTest 5/5、直接安全网 209/209 项断言和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1449 项，1442 通过、7 项环境门控跳过、0 失败，总耗时 481.12 秒。其他尚未逐条归属的日志路径及目标 S3/多实例环境门控仍未收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.45 Metrics 快照失败日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `/metrics` 数据库快照失败的显式关联合同。`MetricsController` 现在在查询前从既有请求属性建立固定 `operation=metrics` 的 `LogContext`，`MetricsService::Render` 按值接收且为非 HTTP 兼容调用保留默认空上下文。PostgreSQL 快照失败只写固定 `WARN` application 消息 `Metrics database snapshot failed`，不再把实例 ID 拼入 message，也不记录 SQL、异常正文、endpoint、连接信息或凭据；顶层 instance ID 仍由结构化日志器写入，upload/job/lease/version 保持 JSON `null`。Prometheus 文本、查询、进程内指标、HTTP 200 和 `disk_metrics_snapshot_success 0` 降级语义不变。

`MetricsServiceLogContextContractTest` 锁定 Controller 到 Service 的单次传播、一个默认上下文、唯一相关 `WARN`、固定消息以及异常正文/消息内实例 ID 禁止项。增强后的 `test_postgres_failover_semantics.py` 在两个真实 API 经稳定 TCP 端点连接物理主备 PostgreSQL 时暂停转发，以唯一 request ID 抓取 API A：响应保持 200、同 request/实际 instance、Prometheus content type、失败 gauge 和进程指标；受管 stdout NDJSON 恰有一条同 request/instance/`metrics` 的 `warning` application 事件，四个所有权字段为空且没有空 request 重复。`0600` 原子证据 `.sisyphus/evidence/postgres-failover-semantics-summary.json` 的 SHA-256 为 `17c05d6dacb840f7bd5c275a086811e5d1d58be542f8c81b5db9114a62a945c0`，不保存 request ID、业务标识、密码、令牌或连接串。

完整构建、Python 语法检查、Metrics/真实故障切换聚焦 CTest 10/10、直接主备演练和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1450 项，1443 通过、7 项环境门控跳过、0 失败，总耗时 479.64 秒。其他尚未逐条归属的日志路径与目标 MinIO/云 S3、多实例拓扑门禁仍未收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.46 Worker 轮询运行时日志关联记录（2026-07-22）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 Worker 运行时边界的类型化关联合同。`StorageWorkerRuntime` 的启动与排空事件现在使用固定 `operation=storage_worker_runtime`，聚合轮询完成、`Result` 失败和异常使用固定 `operation=storage_worker_poll`；这些事件位于持久任务认领边界之外，所以 `request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`，不能把某次回调聚合结果关联到单个任务。实际 instance 只由结构化日志器的进程注册值写入顶层字段，运行时对象不再保存仅供 message 拼接的实例 ID 副本；构造参数、合法性校验、连续 drain、空闲定时轮询、异常吞吐和优雅关闭语义不变。

轮询成功消息继续只保留 claimed/succeeded/retried/dead-lettered/ownership-lost 五项有界聚合计数；领域失败和异常分别固定为 `Storage worker poll failed` 与 `Storage worker poll threw`，不记录 `ErrorInfo`、异常正文、SQL、endpoint、连接信息、凭据或消息内实例 ID。新增内存 NDJSON 用例以不同的构造实例值和日志器注册值依次触发成功、领域失败、异常和幂等排空，精确断言 operation、实际 instance、五个空关联字段与敏感标记排除。增强后的 `test_worker_drain_takeover.py` 逐行解析真实 Worker A 的启动/排空事件，并保留 SIGTERM 后不再认领、排空截止、持久租约不改写、B 自然接管以及 B/C 独占竞争的既有不变量；`0600` 证据 `.sisyphus/evidence/worker-drain-takeover-summary.json` 新增两项运行时关联结论，当前 SHA-256 为 `87dae292e643af45979fe5aec86d865ab3b86f0025d1e8f2c5075dd07fcdb0d5`。

完整构建、Python 语法检查、Worker 运行时/真实排空接管聚焦 CTest 7/7、直接三 Worker 演练和 OpenSpec 严格校验 24/24 通过。完整 CTest 首轮共 1451 项，1443 通过、7 项环境门控跳过、1 项失败，总耗时 477.75 秒；唯一失败是既有上传安全网中网络分区恢复后的 readiness 一次返回 503，其余 791 条断言通过，该用例随后聚焦复跑 1/1 通过（109.58 秒）。第二轮完整 CTest 共 1451 项，1444 通过、7 项环境门控跳过、0 失败，总耗时 487.15 秒。其他尚未逐条归属的日志路径与目标 MinIO/云 S3、多实例拓扑门禁仍未收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.47 周期任务播种日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定周期任务播种边界的类型化关联合同。`ScheduledTasks` 的启动与停止现在使用固定 `operation=storage_job_scheduler`，播种完成、仓储异常和周期失败使用固定 `operation=storage_job_seed`；这些事件发生在单个持久任务认领之外，因此 `request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`，不得从 scan ID、dedupe key、aggregate ID、payload、播种计划或聚合计数推断任务所有权。实际 instance 只由结构化日志器的进程注册值写入顶层字段，运行对象不再保存仅供 message 拼接的实例 ID 副本；初始化校验、60 秒周期、首次立即播种、去重、角色归属、停止接收和排空语义不变。

生命周期消息只保留间隔或 in-flight 状态，成功消息只保留 attempted/enqueued/deduplicated 三项有界计数；仓储异常和周期失败分别固定为 `Periodic storage job seed failed` 与 `Periodic storage job seed cycle failed`，不记录 `ErrorInfo`、异常正文、SQL、endpoint、连接信息、凭据或消息内实例 ID。新增 `ScheduledTasksLogContextContractTest` 锁定五条生产日志、operation 数量、固定失败消息和敏感详情排除；增强后的 `test_scheduler_role_cutover.py` 逐行解析真实 Worker 的启动/完成 NDJSON，同时保留 API A/B 无 seeder、Worker 唯一播种六个当前窗口任务以及 API 扩缩容前后完整行快照不变的既有不变量。结构化证据 `.sisyphus/evidence/scheduler-role-cutover-summary.json` 记录两项关联结论，SHA-256 为 `7f650b8a595607aaba208ba3ffd379133448cdfd7904d76eb034ba8b4b71263e`。

完整构建、Python 语法检查、周期播种 GoogleTest 4/4、真实角色切换聚焦 CTest 1/1 和 OpenSpec 严格校验 24/24 通过。最终完整 CTest 共 1452 项，1445 通过、7 项环境门控跳过、0 失败，总耗时 507.18 秒。其他尚未逐条归属的日志路径与目标 MinIO/云 S3、多实例拓扑门禁仍未收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.48 进程排空日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `BeginShutdown` 进程边界的类型化关联合同。API、Worker 与开发兼容 all 角色的 draining、deadline 和 completed 事件现在统一使用固定 `operation=process_runtime`，实际 instance 只由结构化日志器的进程注册值写入顶层字段；这些事件不属于单个请求、上传或持久任务，所以 `request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`。本轮只改变日志上下文与消息边界，不改变幂等信号处理、停止接收/认领/播种的顺序、100 ms 排空检查、超时退出或持久租约自然接管语义。

开始消息只保留角色与 timeout seconds，超时 warning 只保留 API in-flight 计数和 Worker/scheduler drained 布尔值，完成消息保持固定；三者均不在 message 重复实例 ID，也不记录请求、任务、SQL、endpoint、凭据或异常正文。`ProcessDrainLogContextContractTest` 锁定 2 条 info、1 条 warning、3 个固定 operation 以及空所有权/无手工实例约束；增强后的 `test_worker_drain_takeover.py` 逐行解析真实 Worker A 的进程 draining/deadline NDJSON，并继续验证 SIGTERM 后停止认领、排空截止、持久租约不改写、B 自然接管和 B/C 独占竞争。`0600` 证据 `.sisyphus/evidence/worker-drain-takeover-summary.json` 的 SHA-256 为 `87dae292e643af45979fe5aec86d865ab3b86f0025d1e8f2c5075dd07fcdb0d5`。

完整构建、Python 语法检查、进程运行时 GoogleTest 10/10、真实 Worker 排空接管聚焦 CTest 1/1 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1453 项，1446 通过、7 项环境门控跳过、0 失败，总耗时 495.38 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.49 进程 bootstrap 日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `main` 在实例注册前的类型化关联合同。进程开始、libsodium 成功/失败、运行时配置成功/失败和安全配置校验失败六条事件统一使用固定 `operation=process_bootstrap`；配置尚未验证且 `Logger::SetInstanceId` 尚未调用，因此 `instance_id/request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`，不得从环境、配置或异常文本推断身份。本轮不改变初始化顺序、成功路径或失败路径退出码。

main 的运行时配置与安全校验失败只保留固定阶段摘要，不再拼接 `std::exception::what()`；配置组件自身的受控门禁诊断保持原职责。`ProcessBootstrapLogContextContractTest` 锁定 3 条 info、3 条 error、唯一 bootstrap 上下文、六条固定消息以及无异常正文/手工所有权；增强后的 `test_secure_local_staging_cutoff.py` 解析真实安全模式 API/local staging 启动拒绝 NDJSON，确认固定 error、空实例/所有权，同时保留退出码 1、监听未开放、未超时和既有门禁诊断。结构化证据 `.sisyphus/evidence/local-staging-cutoff-summary.json` 的当前 SHA-256 为 `3e0743c0a3cea4b68c45fd11560bc943ed072ca53cd046bc7302469f99a596d5`。

完整构建、Python 语法检查、进程运行时 GoogleTest 11/11、bootstrap 源码合同与真实启动拒绝聚焦 CTest 2/2 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1454 项，1447 通过、7 项环境门控跳过、0 失败，总耗时 492.85 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.50 运行时配置日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `ConfigMgr` 的类型化关联合同。15 条配置加载/模式 info、3 条默认值 warning 和 6 条安全校验 error 统一使用固定 `operation=runtime_config`；组件只接受结构化日志器已注册的实际实例，正常启动时先于 `Logger::SetInstanceId`，因此 `instance_id` 为 JSON `null`，`request_id/upload_id/job_id/lease_owner/state_version` 也全部保持 JSON `null`，不得从 `m_instance_id` 或环境变量提前复制。

存储路径消息现在只说明显式值或默认值，不记录实际文件系统路径；进程配置摘要保留已验证 role 和受约束的 finalize lease，不在 message 重复 instance。其他诊断只保留已解析的 local/s3、受约束容量/时长/线程/限流数值、JWT 长度和固定环境变量名，不新增 endpoint、bucket、对象前缀、配置文件路径、凭据值或 secret 内容；默认值、环境覆盖优先级、解析、getter、异常正文和安全模式拒绝语义不变。`ConfigMgrLogContextContractTest` 锁定 24 条生产日志、固定上下文及路径/实例排除；真实安全模式 API/local staging 门禁同时解析组件 error 与 main bootstrap 摘要。证据 `.sisyphus/evidence/local-staging-cutoff-summary.json` 的 SHA-256 为 `3e0743c0a3cea4b68c45fd11560bc943ed072ca53cd046bc7302469f99a596d5`。

完整构建、Python 语法检查、ConfigMgr GoogleTest 56/56、配置源码合同与真实启动拒绝聚焦 CTest 2/2 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1455 项，1448 通过、7 项环境门控跳过、0 失败，总耗时 501.81 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.51 存储运行时日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定存储初始化边界的类型化关联合同。`StorageFactory` 的 local/S3 选择以及 `LocalFileStorage`、`LocalBlobStore`、`S3ObjectStorage` 的工作队列初始化共五条事件统一使用共享 `StorageRuntimeLogContext` 和固定 `operation=storage_runtime`；这些进程事件发生在 HTTP 请求和持久任务认领之外，实际 instance 只由结构化日志器的注册值写入，`request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`。

消息只保留固定 backend 和经过配置校验的 I/O、组装线程或最大连接数。S3 初始化不再记录 bucket 和对象 prefix，五条事件也不记录本地路径、endpoint、region、对象 key、凭据、异常正文或消息内 instance；后端选择、S3 bucket 可访问性校验、队列容量、指标注册、构造顺序和失败传播语义不变。`StorageRuntimeLogContextContractTest` 锁定共享上下文、五个调用点、固定消息和 S3 构造区间的部署细节排除；增强后的 `test_worker_observation_mode.py` 在真实 local Worker 中确认工厂、文件和 Blob 三条事件各出现一次、使用实际 instance 和五个空所有权字段，同时保留 readiness/metrics 正常、Ready 任务不变、无租约和不播种语义。该证据随后由 12.52 的同一门禁扩展，当前哈希见 12.52。

完整构建、Python 语法检查、存储运行时/工厂 GoogleTest 4/4、源码合同与真实 Worker 观察模式聚焦 CTest 2/2 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1456 项，1449 通过、7 项环境门控跳过、0 失败，总耗时 491.92 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.52 注册后进程启动日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `main` 在 `Logger::SetInstanceId` 后的类型化关联合同。存储 manager 安装、安装失败和 S3 multipart recovery journal 三个调用点使用共享 `StorageRuntimeLogContext`；TokenService、框架/角色、API application context、Worker 观察模式和初始化完成五个调用点使用固定 `operation=process_runtime`。这些事件都只接受结构化日志器已注册的实际 instance，`request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`，不从配置、角色分支或消息推断所有权。

main 删除了重复的 final/temp 文件系统路径以及 chunk size、文件上限、上传过期、组装并发和 buffer size 七条逐行摘要；ConfigMgr 继续承担有界配置诊断。存储构造或 manager 安装失败改用固定 `Storage manager initialization failed`，不再拼接 `std::runtime_error::what()`；其他启动消息不再手工重复 instance，只在进程配置和完成事件保留 framework version 与有界 role。初始化顺序、TokenService 条件、StorageMgr/BlobStoreMgr 单次安装、S3 journal、beginning advice、API/Worker 分支、观察模式、readiness 标记、退出码和 Drogon 运行语义不变。

`ProcessInitializationLogContextContractTest` 锁定五个 process、三个 storage 调用点、固定消息、无旧式 Logger、无异常正文/消息内 instance 和七项路径/容量 getter 排除；增强后的 `test_worker_observation_mode.py` 在真实 local 观察 Worker 中精确核对四条 storage 与三条 process startup NDJSON 的实际 instance、空所有权和有界消息，同时保留 readiness/metrics、Ready 任务、租约和不播种不变量。`test_blob_gc_process_death.py` 继续通过固定初始化完成事件等待接管 Worker，并完整验证 live lease 不改写及自然接管。结构化证据 `.sisyphus/evidence/worker-observation-summary.json` 的当前 SHA-256 为 `ae38bb4bb9a734840e5ae65992c2f5b73acdbc54a2f38d16e556a9d77c7b7307`。

完整构建、Python 语法检查、进程启动源码合同、真实 Worker 观察模式与 Blob GC 进程死亡接管聚焦 CTest 3/3 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1457 项，1450 通过、7 项环境门控跳过、0 失败，总耗时 504.65 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.53 TokenService 运行时日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定认证进程边界的类型化关联合同。`TokenService` 的单例构造、Auth CPU pool 创建、cache maintenance、metrics timer、周期 pool 指标和 cache eviction 共六条事件统一使用固定 `operation=auth_runtime`；这些事件不属于单个 HTTP 请求、上传或持久任务，实际 instance 只由结构化日志器的进程注册值写入，`request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`。请求级 auth/share 事件继续按值保留调用方上下文。

运行时消息只保留线程数、周期以及 submitted/completed/active/peak、eviction/size 等有界数值，不记录请求上下文、消息内 instance、token/JTI/token hash/cache key、凭据、secret、endpoint 或异常正文；claim、签名、TTL、Redis/CAS、撤销、CPU pool、timer、cache、指标重置、日志级别、错误码和响应语义不变。`TokenServiceLogContextContractTest` 锁定 3 条 debug、3 条 info、唯一运行时上下文、固定消息和无参 Logger 排除；增强后的 `test_auth_cluster_consistency.py` 将测试进程指标周期收紧到 1 秒，在真实 API A 中核对 pool 初始化、timer 启动和周期指标的实际 instance、五个空所有权字段与消息白名单，同时完整保留双 API refresh CAS、跨实例撤销/分享取消、Redis 故障 fail-closed、恢复及 API B 重启不变量。`0600` 结构化证据 `.sisyphus/evidence/auth-cluster-consistency-summary.json` 的 SHA-256 为 `1d2c672bfc0b1271d625ce865369c4f30315b601b766d7cf41f1d834c1399ce4`。

完整构建、Python 语法检查、TokenService 上下文 GoogleTest 4/4、真实双 API 认证一致性聚焦 CTest 1/1 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1457 项，1450 通过、7 项环境门控跳过、0 失败，总耗时 494.17 秒。其他尚未逐条归属的日志路径及目标 MinIO/云 S3、多实例环境门控仍未全部收敛，因此 12.1 的两个总任务继续保持未勾选。

### 12.54 服务初始化日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定服务构造边界的类型化关联合同。Admin、Auth、Cleanup、Content、FileMutation、FileQuery、Folder、OperationLog、Quota、Redis、Share、System、Trash、UploadLifecycle、Upload 和 User 共 16 个初始化调用点统一使用共享 `ServiceRuntimeLogContext` 和固定 `operation=service_runtime`；这些进程事件不属于单个 HTTP 请求、上传或持久任务，实际 instance 只由结构化日志器的进程注册值写入，`request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`。

初始化消息统一为固定 `Service initialized: service=<label>`，label 只取 16 个低基数服务名，不记录请求或领域标识、数据库客户端、endpoint、路径、凭据、secret、token、指针或异常正文；原 DEBUG 级别、构造顺序、依赖注入、Redis 单例保护和业务行为不变。`LogHelperTest` 解析真实内存 NDJSON，验证实际 instance、固定 operation、消息和五个空所有权字段；`ProcessInitializationLogContextContractTest` 跨 16 个生产源文件锁定唯一共享 helper、固定 label、无旧式初始化消息，并确认服务目录只剩 Upload task cache maintenance timer 一处无参 Logger。Content、Quota、Redis 和 OperationLog 的既有源码合同同步收紧为类型化初始化调用。

完整构建、服务初始化上下文 GoogleTest 6/6 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1457 项，1450 通过、7 项环境门控跳过、0 失败，总耗时 491.31 秒。Upload task cache maintenance timer 及其他尚未逐条归属的日志路径仍待后续收敛，目标 MinIO/云 S3 和多实例环境门控也未全部执行，因此 12.1 的两个总任务继续保持未勾选。

### 12.55 上传缓存运行时日志关联记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定 `UploadService` 上传任务缓存维护 timer 的类型化进程边界。可用 event loop 成功注册过期条目淘汰 callback 后，唯一 DEBUG 事件使用 `UploadService.cpp` 局部 `UploadRuntimeLogContext` 和固定 `operation=upload_runtime`；实际 instance 只由结构化日志器的进程注册值写入，`request_id/upload_id/job_id/lease_owner/state_version` 全部保持 JSON `null`。

消息只保留有界 `interval_seconds=60`，不记录 cache key、upload/user ID、请求上下文、endpoint、路径、凭据、secret、token、存储指针或异常正文；event loop 空值判定、`runEvery` 注册、60 秒周期、callback、锁和过期淘汰语义不变。`ProcessInitializationLogContextContractTest` 截取 timer 函数锁定唯一上下文、固定消息、周期常量和敏感值排除，并递归扫描后端 C++ 源码，确认 `src/` 的 Trace/Debug/Info/Warn/Error/Fatal 均不再存在无参 Logger 调用。

完整构建、上传缓存 timer 源码合同 GoogleTest 1/1 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1457 项，1450 通过、7 项环境门控跳过、0 失败，总耗时 492.91 秒。本轮关闭后端无参 Logger 子项，但目标 MinIO/云 S3、多实例环境门控与跨 API/DB/Worker/S3 总追踪验收尚未全部执行，因此 12.1 的两个总任务继续保持未勾选。

### 12.56 显式日志上下文编译期门禁记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定显式日志上下文的编译期合同。Logger 的 Trace、Debug、Info、Warn、Error、Fatal 与高频 detail/success/failure 九个入口删除默认空 `LogContext`，直接 `LogStream` 构造也必须同时传入 level 和上下文。公共头文件变更后全部生产与测试依赖已完整重编译，没有遗留依赖默认参数的生产路径；有意空关联的早期启动和采样测试改为显式 `LogContext{}`。

`LogHelperTest` 的 C++23 concepts 同时证明九个 Logger 无参调用全部不可用、显式上下文入口全部返回 `LogStream`，且直接流不能仅用 level 构造；`ProcessInitializationLogContextContractTest` 同时锁定公开头文件无 `LogContext context = {}` 并递归排除后端无参 Logger。应用事件与被捕获的 Drogon/Trantor/ORM 框架事件继续经同一 formatter 输出 `request_id/instance_id/operation/upload_id/job_id/lease_owner/state_version`，暂无权威值的字段为 JSON `null`；级别、采样、instance 注册和消息格式不变。

完整构建、Logger/LogStream 显式上下文 GoogleTest 6/6 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1457 项，1450 通过、7 项环境门控跳过、0 失败，总耗时 498.79 秒。结构化信封字段总项现有实现、内存 NDJSON、框架捕获、编译期入口与全后端源码门禁共同证明，因此 12.1 第一项已勾选；目标 S3/多实例环境的跨 API/DB/Worker/S3 总追踪仍未完成，第三项保持未勾选。

### 12.57 跨 API/DB/Worker/S3 追踪验收记录（2026-07-25）

OpenSpec、部署运维、系统测试和单元测试文档先行固定端到端存储追踪合同。应用进程在加载 Drogon 配置后把 Trantor 的 TRACE/DEBUG/INFO/WARN/ERROR/FATAL 映射到结构化 spdlog logger，保证 `app.log.log_level` 同时控制框架与应用事件；上传初始化在 PostgreSQL 创建权威任务后立即把真实 `upload_id` 写回日志上下文，后续分片、完成、S3 和 Worker 事件不再依赖消息正文反推任务标识。

`S3AppFlowIntegration` 为 init、chunk、complete、完整下载和 Range 下载分别发送唯一调用方 `X-Request-Id`，使用固定 `instance_id` 解析应用 NDJSON，并将响应 `request_id`、PostgreSQL `upload_tasks`/`upload_task_chunks`/`storage_jobs`/`file_contents`、实际 S3 staging/final key 与类型化日志逐项关联。断言覆盖 init 返回权威 `upload_id`、chunk 与 S3 put、complete 的已提交 `state_version` 和空 lease、multipart complete、完整/Range S3 get、cleanup Worker 认领、S3 delete 及成功终态；持久 cleanup `job_id`、`upload_id`、owner 和去重键均与数据库一致。

Python 语法检查、完整构建 421/421、日志/上传生命周期/Worker/S3 聚焦 CTest 82/82、受控 Moto 5.2.2 的 `S3StorageAdapterIntegration`/`S3AppFlowIntegration` 2/2 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1458 项，1451 通过、7 项环境门控跳过、0 失败，总耗时 508.66 秒；首次完整运行曾有一个历史毫秒级分享过滤器用例瞬态阻塞，终止后单独复跑 1/1 通过，带 120 秒单项上限的完整重跑未复现。以上证据关闭 12.1 第三项；Moto 只作为受控 S3 协议回归端点，不替代目标 MinIO/云 S3、多实例随机路由、故障注入、高可用或发布验收，这些 Phase 8 和最终 Definition of Done 门禁继续保持未勾选。

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

`test_safety_upload_invariants.py` 对未过期和按 PostgreSQL 截止时间过期的任务分别从同一屏障并发发起 complete、cancel 与 expire，已在完整 CTest 中通过；它验证唯一合法终态、租约/分片清零、唯一 staging cleanup，以及 reserved/used、文件、内容行和 ref_count 不变量。`test_distributed_flow.py` 使用 API A 完成/过期扫描、API B 取消，并重复同一套数据库与 S3 对账；Compose/本地多实例入口始终受显式环境变量门控。2026-07-30 已在启用 KVM 的隔离 Docker daemon 中用完整候选镜像执行 Compose 入口并通过 20 项检查；该容器门禁不计作目标环境的 TLS/KMS、依赖高可用、独立故障域或长稳验收。

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
`3e0743c0a3cea4b68c45fd11560bc943ed072ca53cd046bc7302469f99a596d5`。目标环境仍须先保存
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
`7f650b8a595607aaba208ba3ffd379133448cdfd7904d76eb034ba8b4b71263e`。本机门禁证明应用角色
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
`87dae292e643af45979fe5aec86d865ab3b86f0025d1e8f2c5075dd07fcdb0d5`）。该本机隔离演练证明应用、
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

- [x] 删除生产路径对 sticky session 和本地上传暂存的依赖。
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

### 15.12 生产无粘性与共享暂存依赖收尾记录（2026-07-25）

OpenSpec 与部署运维文档先行收敛生产参考拓扑：API/Worker 必须同时启用 secure mode、S3 final 和 S3 upload staging；`/var/lib/disk` 与 `/tmp` 只允许使用可丢弃 `emptyDir` 作为请求临时文件和运行时 scratch，不得以 `hostPath`、PVC 或 Compose 应用卷承载 Blob、分片或组装结果的正确性。Kubernetes Service 固定 `sessionAffinity=None`，Nginx 继续拒绝 cookie/sticky/`ip_hash`，静态检查不替代真实双 API 随机路由验收。

`DistributedTopologyContract` 逐角色锁定 Kubernetes 的两个 `emptyDir` mount、Compose 四个应用进程无本地业务卷、分布式 final/staging S3 默认值、Service 无会话亲和和 Nginx 无粘性策略；同一脚本把 Phase 10 完成状态绑定到这些实现断言。`ConfigMgrJwtTest` 和 `SecureLocalStagingCutoffIntegration` 继续证明 secure API 在监听前拒绝 local staging，而显式 secure Worker 仍可处理已盘点存量 local 描述符。保留这条 legacy Worker 兼容路径是迁移排空要求，不会重新开放生产 local 任务创建，也不构成最终拓扑的节点目录依赖。

Python 语法检查、完整构建无增量工作、安全启动/生产存储/无粘性拓扑聚焦 CTest 4/4 和 OpenSpec 严格校验 24/24 通过。完整 CTest 共 1458 项，1451 通过、7 项环境门控跳过、0 失败，总耗时 509.89 秒。以上仓库级证据关闭 Phase 10 第一项；当前主机没有 Kubernetes API Server、Nginx、Docker 或其他容器运行时，未执行候选镜像、服务端 dry-run、真实 Nginx、目标 MinIO/云 S3 或双 API 随机路由，因此 Phase 6、Phase 9 和最终 Definition of Done 的目标环境门禁继续保持未勾选。

### 15.13 兼容退役准入快照记录（2026-07-25）

审计确认 `upload_task_creation_enabled` 仍是回滚冻结的启动期安全开关，`worker_claiming_enabled` 仍承担观察与排空语义；`upload_tasks.temp_path`、local backend、可空分片描述字段以及 guarded expand rollback 也继续受存量任务和 contract-readiness 合同保护。目标环境尚未完成实际 TTL 后独立小时扫描、全量四 scope 对账、V005 设计/恢复演练审批和退役日期记录，直接删除任一项都会破坏当前迁移或回滚合同，因此 Phase 10 第二项不能据仓库空夹具提前勾选。

OpenSpec、部署运维和系统测试文档先行新增可执行准入合同。`deploy/contract-readiness.sql` 强制传入受控记录中的 `T_s3_only` 与 reconciliation `scan_id`，缺参在读取应用表前稳定失败；有效调用只在一个 `REPEATABLE READ READ ONLY` 事务中读取，输出唯一 schema-v1 JSON，集中包含 12 类 blocker、四 scope 页数/成功状态、`contract_design_review_admitted` 与固定为 `false` 的 `compatibility_removal_allowed`。输出不包含任务 ID、对象 key、endpoint 或凭据，全零结果只准入独立 V005 设计评审，不执行 DDL 或兼容删除。

增强后的 `ContractReadinessCycleIntegration` 在真实隔离 PostgreSQL、Redis、Moto S3、S3-only API 和延后 Worker 上完成自然到期、cleanup、后续上传/下载及四 scope 对账；两种缺参均以 psql 退出码 3 拒绝，有效快照的 12 类 blocker 与独立查询逐项相等且全零，scope 页数一致；同一全零数据库配合未知 scan ID 时 scope 数为 0、design review admission 为 false，两个快照的 compatibility removal 均为 false。脚本前后上传、分片、任务、finding、用户、文件、内容与回收站全表指纹不变。Python 语法检查、完整构建无增量工作、聚焦 CTest 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1458 项，1451 通过、7 项环境门控跳过、0 失败，总耗时 503.52 秒。该批推进 Phase 9 遗留状态可查询能力，但因目标退役日期与批准证据尚缺，Phase 9 第三项、Phase 10 第二项和最终 Definition of Done 继续保持未勾选。

### 15.14 API 实现状态文档收敛记录（2026-07-25）

权威 `docs/design/02-API接口设计.md` 的上传分片章节仍保留“共享 S3/MinIO 暂存和持久孤儿清理仍待实现”的旧阶段说明，与当前 `UploadStagingStorage`、生产安全模式 S3 门禁、持久 `storage_jobs` cleanup/reconciliation 及 local 迁移排空合同冲突。本轮将其改为当前实现边界，并在 `DistributedTopologyContract` 中同时要求三项现状标记存在、旧说明不存在，避免已经交付的分布式能力再次被文档降级。

Python 语法检查、完整构建无增量工作、分布式拓扑文档合同聚焦 CTest 1/1 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1458 项，1451 通过、7 项环境门控跳过、0 失败，总耗时 496.69 秒。该批收敛一个已确认的权威文档漂移，但最终 DoD 的“所有文档与最终行为一致”仍需结合目标环境发布结果做全量终审，因此保持未勾选。

### 15.15 固定 MinIO 测试依赖引导记录（2026-07-26）

OpenSpec、系统测试计划和部署运维文档先行固定无 Docker 主机的依赖准备合同。新增 `scripts/fetch-minio-test-binaries.sh` 只接受显式输出目录和 Linux amd64，下载固定 MinIO `RELEASE.2025-04-22T22-12-26Z`、mc `RELEASE.2025-04-16T18-13-26Z` 的官方 HTTPS archive；文件在硬编码 SHA-256 匹配后才赋予 `0755`，并以同目录硬链接无覆盖原子发布。已有普通文件仅在摘要精确匹配时复用，符号链接、非普通文件或摘要不符均拒绝且不覆盖。

`DistributedTopologyContract` 锁定版本、URL、摘要、平台、HTTPS 和原子发布原语，并以预置错误 `minio` 文件验证拒绝后原文件逐字节不变且没有 `mc`。全新临时目录的真实首次下载与二次复用均通过，两个摘要精确匹配；固定二进制驱动的 `S3ProvisioningIntegration` 1/1 通过（2.69 秒、15 项检查），`DistributedLocalFlowIntegration` 1/1 通过（132.60 秒、17 项跨实例/故障恢复检查），测试结束后受管进程均已清理。Shell 语法、Python 语法、完整构建无增量工作、拓扑合同 1/1 和 OpenSpec 24/24 通过；完整 CTest 共 1458 项，1451 通过、7 项环境门控跳过、0 失败，总耗时 496.47 秒。

该批移除本机真实 MinIO 门禁的手工二进制获取缺口，但单节点 HTTP MinIO、测试随机代理和本机依赖故障不等于目标 TLS/KMS、独立故障域、备份恢复、真实 Nginx 或生产 RTO/RPO。Phase 3/6/9 及最终 DoD 的目标环境门禁继续保持未勾选。

### 15.16 节点本地目录独立性验收记录（2026-07-26）

OpenSpec 与系统测试计划先行新增 `DIST-UPLOAD-005`：一个 S3-native 上传完成后，测试在 A/B 进程存活时只删除并重建各自 `storage_base_path` 与 `temp_upload_path`，随后要求两个 API 分别从同一权威 final key 返回完整对象和同一 Range，且两个业务目录保持为空。`TopologyControl` 为 Compose 与本地进程 runner 提供同一受限操作；调用只接受 `api-a`/`api-b`，Compose 只使用容器内固定 `/var/lib/disk/{blobs,temp}`，本地 runner 只操作隔离临时根下的对应目录，不接受调用方路径。

探索性运行曾清理 Drogon `app.upload_path`，虽然清理后的双 API 下载均成功，但下一次 1 MiB 分片被框架报告为缺少请求体；即使保留目录 inode，仅删除其中临时文件也会复现。该结果确认 `app.upload_path` 是 Drogon 管理大请求体的传输层 scratch，不能在活跃进程中由业务暂存门禁清理。最终合同把它留给框架管理，只删除真正需要证明无业务依赖的 local Blob 与 `temp_upload_path`，随后同一流程的随机路由上传继续跨 A/B 完成。

固定 MinIO 驱动的 `DistributedLocalFlowIntegration` 1/1 通过（133.40 秒），18 项检查包括 A/B 各自完整下载 200、Range 下载 206、实例响应头和目录保持为空；随机入口后续两次分片尝试命中 A/B，任务、文件、内容引用、配额与 final 对象继续唯一。证据 `.sisyphus/evidence/distributed-flow-summary.json` 的 SHA-256 为 `01660961fba551e66ff278566bade07276f6f1d7cd7e79bc905e726a3dc213e9`，受管进程与临时拓扑已清理。Python 语法、完整构建无增量工作、拓扑合同 1/1、完整 CTest 1451 通过/7 跳过/0 失败（505.83 秒）和 OpenSpec 24/24 均通过。因此 Phase 3 的双 API 共享 Blob 读取与删除 `temp_upload_path` 后 S3 暂存两项完成；严格逐请求轮询、大对象真实 MinIO、对象故障工件收敛及目标环境门禁继续保持未勾选。

### 15.17 大对象 multipart copy 与内存上界验收记录（2026-07-26）

OpenSpec 与系统测试计划先行固定 `S3-PROMOTE-001`：真实 MinIO 应用流必须从两个 staging 分片组装 `5 MiB + 17 B` 对象，观察精确 2 次成功的服务端 multipart copy，校验 final 大小/hash、完整/Range 下载，并把 completion 期间 API RSS 增量限制在 48 MiB 本机回归上界内。初始 32 MiB 阈值低于两次真实运行约 36.4 MiB 的峰值增量，因此按实测基线留出回归余量，不把该数值宣称为生产容量。

固定 MinIO 驱动的 `S3AppFlowIntegration` 1/1 通过（10.99 秒），实测 RSS 起始/峰值/增量为 48,390,144 / 86,724,608 / 38,334,464 字节，final 对象大小为 5,242,897 字节。`0600` 原子证据 `.sisyphus/evidence/s3-large-promotion-summary.json` 的 SHA-256 为 `8e8d8c3c33ab9dc72a2bdaf7da1d62794eeab9b6085f4deed9d32f4cbe492981`；C++ multipart/promotion 聚焦测试 6/6、拓扑合同 1/1、OpenSpec 24/24 和完整 CTest 1451 通过/7 环境门控跳过/0 失败（511.50 秒）均通过。因此 Phase 3 大对象组装/提升与内存上界项已完成；严格逐请求轮询、对象故障 multipart/staging 工件收敛、目标环境 HA 及最终 DoD 仍保持未勾选。

### 15.18 上传生命周期严格逐请求交替验收记录（2026-07-26）

OpenSpec 与系统测试计划先行新增 `DIST-UPLOAD-006`：同一 S3-native 上传的 init、chunk 0、chunk 1 和 complete 必须按 API A/B/A/B 顺序执行，每次响应头均须证明实际处理实例。流程必须另外对账任务终态、唯一文件/内容引用、reserved-to-used 配额、S3 final 大小和 API A 逐字节下载，不能只凭直连请求返回 200 判定成功。

固定 MinIO 驱动的隔离 PostgreSQL、Redis、双 API、双 Worker 流程 1/1 通过（133.82 秒、19 项检查），精确序列为 `disk-api-a -> disk-api-b -> disk-api-a -> disk-api-b`，最终文件与 Blob 经统一回收路径清理。分布式证据写入同时收敛为同目录临时文件、`fsync`、`0600` 和 `os.replace`；实测文件 mode 为 `0600`，无残留临时文件，SHA-256 为 `bd5604c5792da42ebc085cfd59fc50922c492fe1d6f433da011e057c4a504eea`。Python 语法、完整构建无增量工作、拓扑合同 1/1、OpenSpec 24/24 和完整 CTest 1451 通过/7 环境门控跳过/0 失败（495.14 秒）均通过。因此 Phase 3 严格逐请求交替项已完成；对象故障 multipart/staging 工件收敛、目标环境 HA 及最终 DoD 仍保持未勾选。

### 15.19 上传仓储死接口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定仓储公开面合同：只保留有生产调用的 PostgreSQL 原语，同时把 C++ 无调用接口清理与数据库/API/存储迁移兼容退役分开。全仓库符号和调用点扫描确认 7 个方法只存在于声明、实现或源码字符串测试，没有业务调用者；其声明与 81 行实现已删除，测试改为防止旧接口回归并继续锁定活跃生命周期原语。

生产路径继续使用所有权查询、完成租约 CAS、取消/过期条件迁移、分片幂等写入/列表及事务连接版 `DeleteChunks`。`Failed` 的数值和读取语义、`temp_path` 兼容回退、local staging 与可空分片描述字段没有删除；`compatibility_removal_allowed=false` 的退役准入仍有效，所以 Phase 3 “迁移完成后删除”总项继续保持未勾选。

完整构建、仓储 GoogleTest 11/11、真实 PostgreSQL 状态机 1/1、基础上传流 20/20、上传安全不变量 885/885、分布式拓扑合同 1/1 和 OpenSpec 24/24 通过。完整 CTest 共 1459 项，1452 通过、7 项环境门控跳过、0 失败，总耗时 496.96 秒。

### 15.20 空聚合存储接口清理记录（2026-07-27）

OpenSpec、ADR、系统测试和单元测试文档先行固定 capability-specific 存储组合合同。全仓库调用点审计确认 `IFileStorage` 只包含虚析构，`ApplicationContext`、`FileMutationService`、`UploadService` 和 `UploadLifecycleService` 对它仅做未使用的指针透传；实际上传暂存与最终内容能力均已由 `UploadStagingStorage` 和 `IBlobStore` 独立表达。

空标记头文件、`StorageMgr::GetStorage()`、服务透传参数和成员现已删除；`StorageFactory::StorageBundle` 与 `StorageMgr` 直接持有 `UploadStagingStorage`，S3 multipart journal/cleaner 通过该 concrete adapter 获取，最终 Blob 继续由 `BlobStoreMgr` 独立持有。`LocalFileStorage` 与 `S3ObjectStorage` 的暂存实现及按持久描述符回退处理 legacy local 任务的兼容路径未改变；`temp_path`、local staging 和可空迁移字段仍受 `compatibility_removal_allowed=false` 准入约束，因此 Phase 3 总清理项保持未勾选。

新增 `StorageCapabilityBoundaryContractTest` 防止空接口、宽泛 accessor 和能力透传参数回归。完整构建、能力边界与上传生命周期聚焦 CTest 17/17、分布式拓扑合同脚本、OpenSpec 24/24 及完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 495.23 秒。首次完整运行只因库存合同仍写死 1458 项而失败，该既有漂移已同步修正到 1460，并由第二次完整回归验证。

### 15.21 Manager 死探针清理记录（2026-07-27）

OpenSpec、ADR、系统测试和单元测试文档先行要求 singleton manager 公开面与真实调用路径对应。全仓库符号审计确认 `StorageMgr::IsInitialized`、`BlobStoreMgr::IsInitialized` 和 `ProcessRuntimeMgr::IsInitialized` 只存在于各自声明与实现，没有生产或测试调用者；启动装配只需要 set/get 操作，删除这三个静态探针不改变实例生命周期或初始化失败行为。

存储能力合同现同时拒绝两个 storage manager 探针回归，进程初始化合同拒绝 `ProcessRuntimeMgr` 静态探针回归并正向锁定 `ProcessRuntimeState::IsInitialized`。后者仍由 readiness 与 `disk_process_initialized` 指标读取，因此本轮没有删除或替代真实运行时状态。完整构建、聚焦 CTest 11/11、分布式拓扑合同、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 489.95 秒。该清理不涉及 `temp_path`、local staging、feature flag 或 schema 退役，Phase 3 与 Phase 10 对应总项继续保持未勾选。

### 15.22 Local 暂存任意路径删除入口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定暂存清理公开面：单个组装工件只能通过 `DiscardAssembly(session, assembly)` 丢弃，整个会话只能通过 `CleanupSession(session)` 清理。全仓库调用点审计确认 `LocalFileStorage::DeletePath` 没有生产调用者，只被 `FileServiceAtomicity_test.cpp` 的旧模拟流程用于绕过描述符直接删除组装路径。

该公开方法、实现及其独占的 timeout awaiter、超时常量与原子状态现已删除；原子性模拟器在哈希不匹配和重名失败路径改用 `DiscardAssembly`。存储能力合同同时拒绝 `DeletePath` 和孤立 timeout helper 回归，并正向锁定描述符操作。`LocalBlobStore::DeleteBlob` 的持久 GC 删除能力与超时保护、local staging 兼容路由、`temp_path` 和可空迁移字段均未改变，因此 Phase 3 与 Phase 10 总清理项继续保持未勾选。

完整构建、存储能力/工厂/local 暂存/上传原子性与分布式拓扑聚焦 CTest 38/38、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 488.14 秒。

### 15.23 Blob 裸路径存在性接口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定最终 Blob 存在性公开面。全仓库调用点审计确认生产对账只调用 `IBlobStore::BlobExists(BlobDescriptor)`；并行存在的 `Exists(std::filesystem::path)` 没有直接生产消费者，只被 `BlobExists` 默认实现转发以及两个 S3 单元测试调用。

`BlobExists` 现改为纯虚描述符能力，由 `LocalBlobStore` 和 `S3ObjectStorage` 直接实现；裸路径 `Exists` 声明和实现已删除，S3/local 测试统一提交持久化描述符，下载 mock 也不再维护旧入口及其无调用计数。能力合同同时要求 `BlobExists` 存在并拒绝 `IBlobStore::Exists(path)` 回归。持久 `blob_gc` 在内容行删除后继续从任务 payload 调用幂等 `DeleteBlob(storage_path)`；final inventory、legacy local 读取、迁移字段和退役准入均未改变，因此 Phase 3 与 Phase 10 总清理项保持未勾选。

完整构建、对账合同、local/S3 Blob、下载响应与存储能力聚焦 CTest 64/64、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 486.10 秒。

### 15.24 Blob 文件专用读取接口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定最终 Blob 读取公开面。全仓库调用点审计确认生产下载只调用 `OpenBlobRangeForRead(BlobDescriptor, start, length)`；`OpenForRead(path)` 仅由一个 local 单测使用，`OpenBlobForRead` 没有直接调用者，S3 的旧 `OpenForRead` 只返回不支持错误。

`IBlobStore` 现只保留纯虚、存储中立的描述符 Range 流能力；两个文件专用读取方法和公开 `FileStorageReadStream` 已删除。local 适配器在私有实现中完成文件打开、定位和限定长度读取，S3 继续直接使用持久化对象 key 执行 Range Get；源码合同拒绝旧入口和公开文件流适配器回归，local 测试新增偏移、长度截断和 EOF 断言，下载 mock 也只实现 Range 流。本地大文件 sendfile 快速路径、下载完整性检查、持久 Blob GC、local/S3 兼容路由和迁移字段均未改变，因此 Phase 3 与 Phase 10 总清理项保持未勾选。

完整构建、local/S3 Blob、下载响应、存储能力、下载流程与分布式拓扑聚焦 CTest 60/60、local Range 定向复验 1/1、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 487.78 秒。

### 15.25 Final locator 公开构造接口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定 final locator 所有权。全仓库调用点审计确认 `GetFinalStoragePath(hash)` 没有适配器外的生产调用者，只由 local/S3 实现内部和测试用于预测目标路径；下载 mock 也因纯虚接口被迫提供无业务意义的实现。

该方法已从 `IBlobStore`、`LocalBlobStore`、`S3ObjectStorage` 的公开头文件和实现删除。两个适配器只在 `PromoteToFinal` 内构造 SHA-256 locator，上传完成继续将实际 `BlobPromoteResult.path` 持久化到内容行；下载、对账与迁移继续只使用持久化 `BlobDescriptor.storage_path`。local/S3 测试改从提升结果或明确夹具规则验证路径，原 S3 getter 单测由既有提升行为用例覆盖并删除。存量 locator、兼容读取、持久 Blob GC 和迁移字段均未改变，因此 Phase 3 与 Phase 10 总清理项保持未勾选。

完整构建、local/S3 Blob、上传路径/一致性、下载响应、存储能力、真实上传流程与分布式拓扑聚焦 CTest 91/91、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1459 项，1452 通过、7 项环境门控跳过、0 失败，总耗时 489.63 秒。

### 15.26 Blob 裸路径大小接口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定最终 Blob 大小查询边界。全仓库调用点审计确认下载预检和内容对账已经持有完整 `BlobDescriptor`，却仍拆出 `storage_path` 调用 `GetFileSize(path)`；其余直接消费者仅为 local/S3 单元测试和下载 mock。

`IBlobStore` 现只暴露 `GetBlobSize(BlobDescriptor)`，下载预检与对账直接传递持久描述符。local/S3 适配器从描述符 locator 查询文件系统或对象存储的实际大小，绝不回显描述符中的期望 `size`；缺失对象、下载大小不一致 finding 和 S3 对象前缀外拒绝语义保持不变。源码合同正向锁定描述符能力并拒绝旧裸路径入口，local/S3 测试分别证明实际元数据优先于错误的期望大小。legacy locator、持久 Blob GC、迁移字段与退役准入均未改变，因此 Phase 3 与 Phase 10 总清理项保持未勾选。

完整构建、local/S3 Blob、下载响应、存储能力、对账、真实下载流与分布式拓扑聚焦 CTest 66/66、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 484.43 秒。首次完整运行中的唯一失败是拓扑合同拒绝上一提交保留的 1459 项库存记录；本节同步权威计数后由定向复验闭环。

### 15.27 仓储级死信重放入口清理记录（2026-07-27）

ADR、系统测试、单元测试和 OpenSpec 先行固定人工死信重放所有权。全仓库调用点审计确认 `StorageJobRepository::ReplayDeadLetter(job_id)` 只有声明与实现；真实管理员入口早已由 `StorageJobAdminService` 在一个 PostgreSQL 事务内完成 `DeadLetter -> Pending`、字段重置与 `admin.storage_job.replay` 操作日志写入，并通过条件更新保证并发单赢家。

无调用仓储方法及其独立 SQL 已删除。`StorageJobRepository` 继续保留生产 Worker 使用的入队、认领、续租、owner 条件成功/失败回写和引用门禁原语；现有仓储合同通过编译期能力探测拒绝独立重放入口回归，管理员 DTO、上下文、dry-run、确认条件、审计内容和事务实现均未改变。该清理不删除任务状态、表字段、管理 API 或迁移兼容路径，因此 Phase 10 过渡字段/配置/分支总项保持未勾选。

完整构建、仓储/管理重放合同、真实 PostgreSQL 队列与管理员操作、分布式拓扑聚焦 CTest 13/13、OpenSpec 24/24 和完整 CTest 均通过；完整 CTest 共 1460 项，1453 通过、7 项环境门控跳过、0 失败，总耗时 483.37 秒。

### 15.28 Redis 批量命令死接口清理记录（2026-07-27）

系统测试、单元测试、部署运维和 OpenSpec 先行固定共享服务公开面合同。全仓库精确符号与调用点审计确认 `RedisService::MSet`、`MGet`、`MDelete` 只有声明与实现，专属 `KeyValue` 类型、`BuildMSetCommand`、`BuildMultiKeyCommand` 也只服务于该孤立子图；四个相关 GoogleTest 不调用接口，只执行 `SUCCEED()`。

三个无调用方法、专属类型、两个命令构造器及四个占位测试已删除。Redis 公开面从 13 个命令 API/23 条直接日志事件收敛为 10 个 API/18 条事件，源码合同同时拒绝全部旧符号回归；生产仍使用 `Ping`、单键读写/存在性/过期、计数、CAS 和固定窗口原子限流，日志上下文、脱敏、依赖指标与失败分类语义不变。本轮不删除 Redis 数据、键空间、配置、兼容分支或部署能力，因此 Phase 10 总清理项继续保持未勾选。

完整构建、Redis/认证集群/持久化/故障切换聚焦 CTest 18/18 和 OpenSpec 24/24 通过。除库存拓扑合同外的完整主体回归 1455/1455 通过，其中 1448 项通过、7 项环境门控跳过、0 失败，耗时 485.36 秒；同步权威库存后拓扑合同 1/1 定向复验通过，合并覆盖完整 1456 项，其中 1449 项通过、7 项环境门控跳过、0 失败。

### 15.29 Worker 启动状态死探针清理记录（2026-07-27）

系统测试、单元测试、部署运维和 OpenSpec 先行固定 Worker 运行时公开面合同。全仓库精确符号与调用点审计确认 `StorageWorkerRuntime::IsStarted()` 只有声明与实现，没有生产、测试或文档调用者；内部 `m_started` 原子状态仍由 `Start()` 的 CAS 使用，`IsDrained()` 仍由进程关停流程读取。当时保留的测试观察探针 `IsAccepting()` 后续已按 15.59 清理。

无调用的 `IsStarted()` 声明与实现已删除，`StorageWorkerRuntime_test.cpp` 通过编译期能力探测拒绝旧公开探针回归。启动幂等、首次及周期轮询、连续 drain、异常吞吐、in-flight 排空等待和结构化日志语义均未改变。本轮不删除 Worker 状态、配置、持久任务字段、迁移分支或部署能力，因此 Phase 10 总清理项继续保持未勾选。

完整构建、Worker 运行时/进程关停/真实排空接管/分布式拓扑聚焦 CTest 17/17 和 OpenSpec 24/24 通过。完整 CTest 共 1456 项，1449 项通过、7 项环境门控跳过、0 失败，总耗时 487.81 秒。

### 15.30 分享时间戳死 helper 清理记录（2026-07-27）

系统测试、单元测试、部署运维和 OpenSpec 先行固定分享更新时间所有权。全仓库精确符号与调用点审计确认 `ShareService::UpdateTimestamp()` 只有私有声明与实现，没有生产、测试或文档调用者；分享配置更新通过模型写入自身 `updated_at`，批量取消通过状态更新 SQL 同步写入该字段。

无调用 helper 的声明与实现已删除，`ShareLogContext_test.cpp` 将服务上下文合同从 23 项收敛为 22 项，并通过声明与定义双重否定断言拒绝旧 helper 回归。访问与下载仍只更新 view/download count 和文件下载元数据，分享管理、访问、下载、审计和失败语义均未改变。本轮不删除分享状态、数据库字段、API、配置或迁移兼容路径，因此 Phase 10 总清理项继续保持未勾选。

完整构建、分享管理/浏览/下载聚焦 CTest 283/283 和 OpenSpec 24/24 通过。完整 CTest 共 1456 项，1449 项通过、7 项环境门控跳过、0 失败，总耗时 486.24 秒。

### 15.31 配置凭据死 getter 清理记录（2026-07-27）

系统测试、单元测试、部署运维和 OpenSpec 先行固定运行时凭据所有权。全仓库精确符号与调用点审计确认 `ConfigMgr::GetDatabasePassword()` 与 `GetRedisPassword()` 都只有公开声明与实现，没有生产、测试或文档调用者；启动流程在 `drogon::app().loadConfigJson()` 前调用 `RuntimeConfig::LoadFromEnvironment()`，由其环境覆盖把凭据写入唯一 `default` 数据库客户端与 Redis 客户端配置。

两个无调用 getter 的声明与实现已删除，`ConfigMgr_test.cpp` 通过头文件和实现四项否定断言拒绝旧凭据读取面回归。`RuntimeConfig::LoadFromEnvironment()` 内部环境覆盖阶段的注入、唯一数据库客户端门禁和 `ValidateSecureConfig()` 的安全模式非空检查均未改变；真实 PostgreSQL/Redis 连接、跨实例认证一致性和 Redis 会话持久化保持原语义。本轮不删除环境变量、部署 Secret、Drogon 客户端字段、兼容配置或迁移分支，因此 Phase 10 总清理项继续保持未勾选。

完整构建、配置/安全模式/真实 PostgreSQL 与 Redis 聚焦 CTest 69/69 和 OpenSpec 24/24 通过。完整 CTest 共 1456 项，1449 项通过、7 项环境门控跳过、0 失败，总耗时 495.22 秒。

### 15.32 Auth CPU 指标死包装器清理记录（2026-07-27）

系统测试、单元测试、部署运维和 OpenSpec 先行固定 Auth CPU pool 指标所有权。全仓库精确符号与调用点审计确认 `detail::StartAuthCpuPoolMetricsTimer()` 与 `GetAuthCpuPoolActiveTaskCount()` 都只有声明与实现，没有生产、测试或文档调用者；`RunOnAuthCpuPool` 继续通过 `GetAuthCpuWorkLoop()` 取得工作循环，API beginning advice 继续通过 `StartCacheMaintenance()` 启动维护与指标 timer。

两个无调用包装器的声明与实现已删除，`TokenServiceLogContext_test.cpp` 通过头文件/实现否定断言拒绝旧符号回归，并正向锁定工作循环、`StartPoolMetricsTimer(metrics_interval)` 和 `LogPoolMetrics()` 周期回调。TokenService 内部 submitted/completed/active/peak 原子计数、周期重置、`auth_runtime` 日志和请求跨 CPU pool 执行均未改变；真实双 API 仍产生 pool 初始化、1 秒 timer 启动和周期指标。本轮不删除线程池、配置键、指标字段、认证 API 或迁移兼容路径，因此 Phase 10 总清理项继续保持未勾选。

完整构建、Token/认证过滤器/真实双 API auth runtime 指标聚焦 CTest 127/127 和 OpenSpec 24/24 通过。完整 CTest 共 1456 项，1449 项通过、7 项环境门控跳过、0 失败，总耗时 488.09 秒。

### 15.33 文件仓储后代路径死原语清理记录（2026-07-28）

系统测试、单元测试、部署运维和 OpenSpec 先行固定文件仓储路径更新职责。全仓库精确符号与调用点审计确认 `FileRepository::UpdateDescendantFilePathsForFolderMove()` 只有声明与实现，其专属路径前缀 SQL 也只由该方法引用，没有生产或行为测试调用者；既有测试只是正向断言该孤立 SQL 存在。

无调用方法、专属 SQL 与过时正向断言已删除。`FileRepository_test.cpp` 现在通过头文件/实现否定断言拒绝旧入口与 SQL 回归，并正向锁定 `FolderService` 重命名和 `FileMutationService` 移动在事务内逐个调用 `UpdateFilePath()`、按当前文件名重建路径。真实嵌套文件夹移动继续对账根与后代文件夹路径、后代文件路径和 item count；用户谓词、事务边界、公开响应、迁移字段与兼容路径均未改变，因此 Phase 10 总清理项继续保持未勾选。

完整构建、文件/文件夹仓储与移动合同、真实文件夹生命周期/嵌套移动安全网、分布式拓扑聚焦 CTest 14/14 和 OpenSpec 24/24 通过。完整 CTest 共 1456 项，1449 项通过、7 项环境门控跳过、0 失败，总耗时 499.45 秒。

### 15.34 文件仓储死客户端状态清理记录（2026-07-28）

系统测试、单元测试、部署运维和 OpenSpec 先行固定文件仓储连接所有权。全仓库精确读写与实例化审计确认 `FileRepository` 的构造参数只写入 `m_db_client`，该字段从未被任何方法读取；6 个生产读取/更新原语都显式接收 `DbClientPtr`，调用方按操作传入默认客户端或当前 transaction。

无读取字段、注入构造、仅为构造服务的 `<utility>` include 以及 `FileMutationService`、`FolderRepository`、`FolderService` 的四个旧构造点已删除。`FileRepository_test.cpp` 通过编译期断言要求仓储可默认构造且为空类型，通过源码合同锁定头文件/实现各 6 个显式 client 参数、拒绝旧字段/构造回归并核对三个生产调用方使用默认构造。文件重命名/移动、文件夹重命名、删除计划、事务连接、用户谓词、SQL、路径结果和公开响应均未改变，因此 Phase 10 总清理项继续保持未勾选。

完整构建、无状态文件仓储/文件夹仓储/移动合同、真实文件夹生命周期/嵌套移动安全网、分布式拓扑聚焦 CTest 15/15 和 OpenSpec 24/24 通过。完整 CTest 共 1457 项，1450 项通过、7 项环境门控跳过、0 失败，总耗时 504.68 秒。

### 15.35 内容引用 unchecked 死包装器清理记录（2026-07-28）

系统测试、单元测试、部署运维和 OpenSpec 先行固定内容引用批量递增的错误所有权。全仓库精确调用点审计确认 `ContentService::IncrementRefCounts()` 只有声明、实现与编译期测试，没有生产调用者；该方法只调用 `IncrementRefCountsChecked()`，却会将任何错误降级为空集合并记录一条不可达 warning。

无调用 unchecked 包装器及其独占 warning 已删除。`ContentService_test.cpp` 通过声明、定义和固定日志三项否定断言拒绝旧路径回归，并正向锁定 `FileMutationService` 中两个复制事务直接调用 checked 版本。两处调用仍检查 `Result` 并传播错误，引用计数 SQL、行锁、reference gate、用户谓词、事务回滚与公开响应语义均未改变；Phase 10 总清理项继续保持未勾选。

完整构建、内容服务合同、真实复制/文件变更、配额/移动安全网与分布式拓扑聚焦 CTest 9/9 和 OpenSpec 24/24 通过。完整 CTest 共 1457 项，1450 项通过、7 项环境门控跳过、0 失败，总耗时 509.97 秒。

### 15.36 文件夹仓储死客户端状态清理记录（2026-07-28）

系统测试、单元测试、部署运维和 OpenSpec 先行固定文件夹仓储的连接所有权。全仓库精确读写与实例化审计确认 `FolderRepository` 构造参数只写入 `m_db_client`，该字段从未被任何方法读取；当前 12 个活跃归属/子树/批量删除计划/树形/面包屑/路径/计数操作都显式接收调用级 standalone client 或当前 transaction。最初计入的单项删除计划原语随后按 15.63 清理。

无读取字段与注入构造已删除，当时 `FileServiceUtils`、`UploadLifecycleService`、`FolderService` 和 `FileMutationService` 的 7 个生产构造点改为默认构造；清理单项删除计划后现存 6 个构造点。`FolderRepository_test.cpp` 通过编译期断言要求仓储可默认构造且为空类型，通过源码合同拒绝旧字段/构造与单项删除计划回归，并核对头文件与实现各 12 个显式 client 参数及全部现存生产构造点。参数化 SQL、用户谓词、事务连接、路径/计数更新、日志上下文和公开响应均未改变；Phase 10 总清理项继续保持未勾选。

完整构建、文件/文件夹仓储与移动合同、真实文件变更/文件夹生命周期/上传流程、配额/移动安全网与分布式拓扑聚焦 CTest 19/19 和 OpenSpec 24/24 通过。完整 CTest 共 1457 项，1450 项通过、7 项环境门控跳过、0 失败，总耗时 518.21 秒。

### 15.37 操作日志通用死写入子图清理记录（2026-07-28）

系统测试、单元测试、部署运维和 OpenSpec 先行固定操作日志的查询边界与写入所有权。全仓库精确符号与调用点审计确认 `OperationLogService::Log()` 只有声明和实现；其专属 `OperationLogEntry`、`ActionType`、`TargetType`、IP 归一化和枚举转换 helper 也没有生产消费者。

通用死写入子图与 9 个只测死代码的用例已删除，`OperationLogService` 收敛为用户可见的 `GetList()` 分页查询。源码合同拒绝旧符号回归，并正向锁定 `AuthService`、`ShareAuditService`、`AdminService`、`StorageJobAdminService` 和 `StorageRecoveryAdminService` 的真实审计写入。`/api/logs` 请求/响应、已存审计数据、各领域事务归属及 fail-open/fail-closed 策略均未改变；Phase 10 总清理项继续保持未勾选。

完整构建、日志查询/运行时/领域审计/安全上传聚焦 CTest 23/23 和 OpenSpec 24/24 通过。完整 CTest 共 1448 项，1441 项通过、7 项环境门控跳过、0 失败，总耗时 518.96 秒。

### 15.38 配置令牌 TTL 死公开面清理记录（2026-07-29）

系统测试、单元测试、部署运维和 OpenSpec 先行固定 access/refresh token 寿命的唯一所有权。全仓库精确读写与调用点审计确认 `ConfigMgr::GetAccessTokenExpireSeconds()`、`GetRefreshTokenExpireSeconds()` 只返回各自的固定成员；两个 getter 与两个成员都没有生产或测试消费者，也没有 JSON/环境加载路径。

这组平行死 getter、成员与头文件中误导为可配置 TTL 的过时示例已删除。`ConfigMgr_test.cpp` 通过头文件/实现否定断言拒绝旧配置面回归，并正向锁定 `TokenService` 的 7200/604800 秒常量及签发、认证响应和撤销缓存调用链。JWT 签名/验证、Redis key 寿命、refresh CAS、API `expires_in`、安全模式、迁移字段和兼容分支均未改变；Phase 10 总清理项继续保持未勾选。

完整构建、配置/Token/认证集群/Redis 会话与故障切换聚焦 CTest 121/121 和 OpenSpec 24/24 通过。完整 CTest 共 1448 项，1441 项通过、7 项环境门控跳过、0 失败，总耗时 501.85 秒。

### 15.39 上传状态机测试专用死规则清理记录（2026-07-29）

ADR、系统测试、单元测试和 OpenSpec 先行固定上传状态机的生产决策边界。全仓库精确调用点审计确认 `IsAllowedTransition`、`CanRenewFinalizeLease`、`CanCommitFinalizeLease`、`CanComplete` 和 `CanCancelOrExpire` 只有声明、实现与 `UploadStateMachine_test.cpp` 中的直接调用，没有生产、集成或工具消费者。

五个未接入运行时的平行纯函数规则及三个专属单测已删除。`UploadTaskRepository_test.cpp` 通过头文件/实现否定断言拒绝旧符号回归，并正向锁定 `DecideFinalizeRequest`、`DecideCancelRequest` 与真实仓储调用。状态解析、诊断名称、终态判定、完成认领/接管/重放与取消幂等语义保持不变；真实租约续期和提交仍由 PostgreSQL 状态/owner/version/到期条件写执行，并由真实数据库竞态测试直接覆盖。Phase 10 过渡字段/配置/分支总清理项继续保持未勾选。

完整构建与 OpenSpec 24/24 通过。聚焦 CTest 首轮 33/34 通过，唯一失败是前次运行残留的 Redis 管理员限流计数在预期 409 分支前拦截 `StorageJobOperationsIntegration`；限流窗口到期后该用例定向复验 1/1 通过，随后完整 CTest 也包含其通过结果。完整 CTest 共 1445 项，1438 项通过、7 项环境门控跳过、0 失败，总耗时 501.00 秒。

### 15.40 分享令牌 allowlist 死 key 清理记录（2026-07-29）

ADR、API、系统测试、单元测试和 OpenSpec 先行固定分享令牌的 Redis 状态边界。全仓库精确读写与调用点审计确认 `SHARE_TOKEN_PREFIX` 与 `RedisKeyPrefix::BuildShareTokenKey()` 只被 `RedisKeyPrefix_test.cpp` 的三个格式用例读取，没有生产、集成、工具、部署或迁移消费者；运行时签发自包含分享 JWT，从未写入或查询 `share_token:{share_code}:{token_hash}` allowlist。

旧常量、builder 与三个只测死代码的用例已删除。`RedisKeyPrefix_test.cpp` 新增编译期 concept 断言，同时拒绝常量和可调用 builder 回归；既有活跃用例继续锁定 refresh、access/share blacklist、文件列表缓存、登录/分享/API/上传限流 key 格式。分享 JWT 签名/scope/TTL、blacklist fail-closed、取消后 PostgreSQL 实时状态失效和公开 API 响应均未改变；Phase 10 旧 schema/feature flag/迁移分支总清理项继续保持未勾选。

完整构建、分享 JWT/blacklist/限流、双 API 取消、Redis 持久化/故障切换聚焦 CTest 63/63 和 OpenSpec 24/24 通过。完整 CTest 共 1442 项，1435 项通过、7 项环境门控跳过、0 失败，总耗时 499.95 秒。

### 15.41 文件联合哈希测试专用死子图清理记录（2026-07-29）

ADR、系统测试、单元测试和 OpenSpec 先行固定文件完整性计算的生产所有权。全仓库精确符号与调用点审计确认 `FileHashPair` 与 `FileHashUtil::HashFileMd5AndSha256()` 只有定义和四个专用单元测试，没有生产、集成、工具、客户端或迁移消费者；Local 与 S3 组装实现已在各自的有界单次流式读取中同时计算整文件 MD5 和 SHA-256。

联合结果类型、平行 helper 及四个只测死代码的用例已删除。`FileHashUtil_test.cpp` 通过编译期 concept 断言拒绝旧 helper 回归；生产仍保留 `HashFileMd5()`、`HashFileSha256()` 和增量 MD5 API，Local/S3 `AssembleChunks()` 仍由直接行为用例覆盖。本轮不删除 local staging、schema、feature flag 或迁移兼容分支，因此 Phase 3 与 Phase 10 总清理项继续保持未勾选。

完整构建、文件哈希/Local/S3 组装/真实上传与安全网聚焦 CTest 108/108 和 OpenSpec 24/24 通过。完整 CTest 共 1438 项，1431 项通过、7 项环境门控跳过、0 失败，总耗时 509.58 秒；有界单次流式读取、分片/整文件 MD5、整文件 SHA-256 与最终内容寻址合同保持不变。

### 15.42 MD5 验证测试专用包装器清理记录（2026-07-29）

ADR、系统测试、单元测试和 OpenSpec 先行固定分片 MD5 验证的生产所有权。全仓库精确调用点审计确认 `FileHashUtil::VerifyHash()` 只被 `FileHashUtil_test.cpp` 的四个工具单测和 `UploadPath_test.cpp` 的一个重复单测调用，没有生产、集成、工具、客户端或迁移消费者；生产 `UploadService` 已直接执行 `HashMd5(chunk_payload) != chunk_hash` 判定。

该只包装 `HashMd5(data) == expected_md5` 的公开 helper 及五个只测死代码的用例已删除。`FileHashUtil_test.cpp` 通过编译期 concept 断言拒绝旧 helper 回归；活跃 `HashMd5()` 原语、分片哈希不匹配的 `ChunkVerifyFailed` 错误、计算值向 staging 落盘路径的传递和公开 API 响应均未改变。本轮不删除 local staging、schema、feature flag 或迁移兼容分支，因此 Phase 3 与 Phase 10 总清理项继续保持未勾选。

完整构建、文件哈希/UploadPath/真实上传/授权与安全网聚焦 CTest 55/55 和 OpenSpec 24/24 通过。完整 CTest 共 1433 项，1426 项通过、7 项环境门控跳过、0 失败，总耗时 505.03 秒。

### 15.43 通用 Result 响应死适配器清理记录（2026-07-29）

OpenSpec、系统测试、单元测试和项目知识库先行固定 Controller 响应边界。全仓库精确符号与调用点审计确认两个 `Response::FromResult` 重载都没有生产、集成、工具、客户端或迁移消费者；只有 `Result<void>` 重载被两条专属单元测试直接调用，泛型重载没有任何调用者。全部 Controller 已在 HTTP 边界显式选择 `Response::Success()`、`Response::Error()` 或 `Response::Paginated()`，启动失败继续使用 `Response::Fail()`。

两个死适配器、只为泛型转发使用的 `<utility>` include 及两条只测死代码的用例已删除。`Response_test.cpp` 通过泛型与 void 两项编译期 concept 断言拒绝旧入口回归；活跃错误信封用例继续锁定稳定业务码、消息、空 data 和 HTTP 状态。统一 JSON 信封、分页、启动失败、Controller 分支及公开 API 响应均未改变；Phase 10 旧 schema/feature flag/迁移分支总清理项继续保持未勾选。

完整构建、响应与 Controller 真实 HTTP 聚焦 CTest 15/15 和 OpenSpec 24/24 通过。完整 CTest 共 1431 项，1424 项通过、7 项环境门控跳过、0 失败，总耗时 506.93 秒。

### 15.44 分片覆盖测试专用规则清理记录（2026-07-29）

ADR、系统测试、单元测试和 OpenSpec 先行固定完整分片覆盖的生产所有权。全仓库精确符号与调用点审计确认 `ChunkCoverage` 与 `IsCompleteCoverage()` 只有声明、实现和两条纯函数单测，没有生产、集成、工具、客户端或迁移消费者。生产完成认领已在 `UploadTaskRepository::ClaimFinalizeLease` 的单条 PostgreSQL 条件更新中同时校验上传/用户归属、状态、数据库时间、分片数量和最大索引。

快照类型、平行覆盖规则和两条只测死代码的用例已删除。`UploadTaskRepository_test.cpp` 通过源码否定断言拒绝旧符号回归，并继续正向锁定活跃认领 SQL；`test_upload_state_machine.py` 继续使用真实 PostgreSQL 直接覆盖缺片拒绝、完整覆盖认领和并发唯一 owner。认领 SQL、租约、状态版本、公开完成响应、schema 与迁移兼容合同未改变；Phase 10 过渡字段/配置/分支总清理项继续保持未勾选。

完整构建、上传生命周期/仓储/真实状态机与安全网聚焦 CTest 26/26 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 504.91 秒。

### 15.45 服务校验死异常类型清理记录（2026-07-29）

系统概述、系统测试、单元测试和 OpenSpec 先行固定服务层业务错误边界。全仓库精确符号审计确认 `ServiceValidationException` 只在 `FileServiceUtils.hpp` 定义，没有构造、抛出、捕获、生产、集成、工具或客户端消费者；既有源码测试只间接检查 `Move` 函数体未使用它。

该死异常类型已删除。`FileMutationServiceMove_test.cpp` 现在直接读取 `FileServiceUtils.hpp` 并拒绝旧结构回归，同时继续正向锁定 `TransactionRunner` 边界与 `std::unexpected(ErrorInfo(...))` 传播。参数化 SQL、事务回滚、错误码/消息、公开 API、schema 与迁移兼容合同未改变；Phase 10 过渡字段/配置/分支总清理项继续保持未勾选。

完整构建、文件变更/文件夹仓储/真实移动安全网与拓扑库存聚焦 CTest 11/11 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 508.33 秒。

### 15.46 DTO 标量测试专用 helper 清理记录（2026-07-30）

系统概述、系统测试、单元测试和 OpenSpec 先行固定 DTO 基类共享解析边界。全仓库精确调用点审计确认 `DtoBase::RequireBool` 与 `DtoBase::OptionalInt` 只有基类定义和 `DtoBase_test.cpp` 中的直接调用，没有生产请求 DTO、集成、工具、客户端或迁移消费者。

两个测试专用 helper 及其专属断言已删除。`DtoBase_test.cpp` 通过源码否定断言拒绝旧符号回归；12 个活跃 JSON/path/query/ID-array helper 继续返回既有 `Result<T>`/`ErrorInfo` 且不产生日志。请求字段、错误码/消息、公开 API、schema 与迁移兼容合同未改变；Phase 10 过渡字段/配置/分支总清理项继续保持未勾选。

完整构建、DTO 行为/源码合同/真实校验失败链聚焦 CTest 3/3 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 499.06 秒。

### 15.47 DTO 序列化死重载清理记录（2026-07-30）

系统概述、系统测试、单元测试和 OpenSpec 先行固定 DTO 基类共享序列化边界。全仓库精确调用点与已构建对象符号审计确认，`DtoBase::SetField` 的 `std::string_view`/`const char*` 重载以及 `DtoBase::SetArray` 的 `std::vector<uint64_t>`/`std::vector<std::string>` 重载只有基类定义，没有生产 DTO、测试、集成、工具、客户端或迁移消费者，也没有对象代码实例化。

四个死重载和随之无用的 `<string_view>` include 已删除。`DtoBase_test.cpp` 通过源码否定断言拒绝旧签名回归；其余 12 个生产序列化重载/模板继续覆盖标量、嵌套对象、可选/nullable、对象数组和 `uint32_t` 数组。请求/响应字段、JSON 类型、null/省略语义、错误码/消息、公开 API、schema 与迁移兼容合同未改变；Phase 10 过渡字段/配置/分支总清理项继续保持未勾选。

完整构建、DTO 序列化/响应与拓扑聚焦 CTest 133/133 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 511.27 秒。

### 15.48 最新候选本地多实例与固定 MinIO 门禁复验记录（2026-07-30）

候选提交 `aad7cf55` 上，仓库固定且通过 SHA-256 校验的 MinIO `RELEASE.2025-04-22T22-12-26Z` 与 mc `RELEASE.2025-04-16T18-13-26Z` 驱动四个显式环境门禁：`DistributedLocalFlowIntegration` 1/1 通过（140.20 秒、20 项检查），`S3ProvisioningIntegration` 1/1 通过（2.60 秒、15 项检查），`S3StorageAdapterIntegration` 1/1 通过（0.38 秒），`S3AppFlowIntegration` 1/1 通过（11.04 秒）。

本地隔离拓扑覆盖无粘性认证/上传/下载、严格逐请求 A/B 交替、并发 complete/cancel/expire、Worker 租约接管、PostgreSQL/Redis/MinIO 停机后两个 API 原进程恢复、单 API 停机后入口继续服务，以及 multipart abort 持久重试收敛。S3 应用门禁复验 `5 MiB + 17 B` 的 2-part server-side copy、全量/Range 下载、持久清理与 48 MiB RSS 增量上界。

三份 `0600` 原子证据的 SHA-256 依次为：`distributed-flow-summary.json` = `ce29f212bd6442293c94c13def468dae43601d400eb7466149ebb9e593198e7b`，`s3-provisioning-summary.json` = `e96c82c3986de1ad1ef57999c535aebaff84c7237938d773c47f7cd3d8681dbf`，`s3-large-promotion-summary.json` = `52413aaed2328e98585389f965d42169bf54e42af47bb7ec4e6bbfd3711c20e8`。受管进程和临时拓扑已清理。

本轮只执行 7 个常规环境门禁中的上述 4 个；`PgBouncerTransactionPoolIntegration`、`PrometheusAlertRulesIntegration` 和容器拓扑 `DistributedFlowIntegration` 未在该候选上复验。单节点 HTTP MinIO 与测试随机代理不替代目标 Nginx/TLS/KMS、高可用 PostgreSQL/Redis/S3、独立故障域、备份恢复、负载/长稳、真实迁移数据与兼容路径退役验收，因此 Phase 6/9/10 和最终 Definition of Done 相关项继续保持未勾选。

### 15.49 固定 Prometheus 规则测试依赖引导记录（2026-07-30）

OpenSpec、系统测试、部署运维和单元测试文档先行固定 Prometheus 规则发布门禁的依赖获取合同。新增 `scripts/fetch-promtool-test-binary.sh` 只接受显式输出目录和 Linux amd64，只下载官方 Prometheus `v3.13.1` 版本化 HTTPS release 归档。脚本先校验归档 SHA-256 `962b812371aff838d152b6ff2d56fdb7a6396f5542f48ebf73421b9721f0d103`，再校验精确提取的 `promtool` SHA-256 `d2344bad40fbd10b8e4dd9ae712e69bab7add68feeed22675cb0b6f1d9e741d8`，两者均匹配后才以同目录硬链接无覆盖原子发布 `0755` 普通文件。

已有 `promtool` 只在二进制摘要精确匹配时复用；符号链接、非普通文件或摘要不符时在下载前拒绝且不覆盖。`DistributedTopologyContract` 锁定版本、URL、双摘要、平台、HTTPS、精确提取和无覆盖发布原语，并以预置错误文件证明拒绝后字节与目录内容均不变。

全新临时目录的真实首次下载、二次复用、二进制摘要/权限/无临时文件检查均通过；固定 `promtool 3.13.1` 驱动 `PrometheusAlertRulesIntegration` 1/1 通过（0.12 秒，总耗时 0.15 秒）。Shell 语法、分布式拓扑合同 1/1 和 OpenSpec 24/24 通过，临时下载及提取产物已清理。最新候选已显式复验 7 个常规环境门禁中的 5 个；`PgBouncerTransactionPoolIntegration` 与容器拓扑 `DistributedFlowIntegration` 仍待复验，目标环境告警投递/值班链路和长稳故障演练也未因规则单测自动完成。Phase 6/9/10 和最终 Definition of Done 相关项继续保持未勾选。

### 15.50 最新候选 PgBouncer 事务池门禁复验记录（2026-07-30）

候选提交 `7a35ada7` 上，从 pgbouncer.org 官方版本化 HTTPS 路径下载 PgBouncer `1.25.2` release tarball，官方校验文件与 GitHub release API 公布的 SHA-256 均为 `924ad35113fd0a71c8e2dbe85b5d03445532e2b7b37a9f8a48983beea238b332`。归档摘要和路径边界复核后，在忽略目录内以 Autotools/GCC、libevent `2.1.13`、c-ares `1.34.8` 和 OpenSSL `3.6.3` 临时构建，二进制版本探针精确返回 `PgBouncer 1.25.2`。

该二进制驱动 `PgBouncerTransactionPoolIntegration` 1/1 通过（CTest 3.54 秒，总耗时 3.56 秒）。两个真实 API 的 8 条 Drogon 客户端连接精确复用到 2 条 PostgreSQL 后端连接；跨实例 ORM 与 `TransactionRunner` 工作流、事务内 `SET LOCAL`/`ON COMMIT DROP` 临时表和 `pg_advisory_xact_lock` 提交前互斥/提交后释放全部通过，`SHOW STATS` 记录 client parse 27、server parse 20、bind 54。

`0600` 原子证据 `.sisyphus/evidence/pgbouncer-transaction-pool-summary.json` 的 SHA-256 为 `93492ee81be9052d71093ffeb6e273e80b8ced06cf84f0481ea73f6235a5a278`，不含端口、路径、凭据或业务标识，受管测试进程均已清理。至此最新候选已显式复验 7 个常规环境门禁中的 6 个，仅容器拓扑 `DistributedFlowIntegration` 未在该候选上复验。本地单 PgBouncer/单 PostgreSQL 流程不替代生产认证/TLS、稳定写端点 HA、独立故障域、连接/内存容量和版本升级回归验收。Phase 6/9/10 和最终 Definition of Done 相关项继续保持未勾选。

### 15.51 最新候选容器拓扑门禁复验记录（2026-07-30）

系统测试与部署运维文档先行固定候选镜像的完整构建合同。`Dockerfile` 构建层显式补齐固定 vcpkg `libpq` port 需要的 `bison`、`flex` 和 `perl`，并把 vcpkg 与项目统一到 `gcc-14`/`g++-14`，避免 Clang 18 与 GCC 14 标准库混用时缺失项目所需的完整 C++23 能力。runtime 账户保留用户名 `disk`，但使用容器专属组 `disk-app` 和数值身份 `10001:10001`，不再与 Ubuntu 预置系统组 `disk` 冲突。`DIST-DEPLOY-004` 同时锁定工具清单、编译器标记、独立组和数值 `USER`。

以候选基线 `86d8e9a9` 的工作树从仓库 `Dockerfile` 完整构建成功；最终 `disk-distributed:local` 镜像 ID 为 `sha256:829c4aedd6f86c14f4252c72773cc4ea6ec60125a3802d7249d6a45df3790d2d`，大小 71664902 字节，`Config.User` 为 `10001:10001`，容器内 `id` 返回 `uid=10001(disk) gid=10001(disk-app) groups=10001(disk-app)`。这一清洁构建实际暴露并关闭了缺少 parser 工具、混用编译器和 runtime 组名冲突三个静态合同未曾证明的可执行制品缺陷。

在启用 KVM 的隔离 Linux Docker daemon 中，Compose 使用该单一镜像启动 PostgreSQL、Redis、MinIO、两个 API、两个 Worker 和真实 Nginx 无粘性入口。`DistributedFlowIntegration` 1/1 通过（CTest 122.29 秒，总耗时 122.31 秒，20 项检查）；严格 A/B 交替、单入口随机路由、终态竞态、Worker 租约接管、PostgreSQL/Redis/MinIO 故障后原 API 进程恢复、multipart abort 持久重试收敛和单 API 停止后入口继续服务均通过。`0600` 原子证据 `.sisyphus/evidence/distributed-flow-summary.json` 的 SHA-256 为 `f129db467a5a84944136e9c59340b333582141b5b2b4f4888d2ae37fb5e30451`，状态为 `passed`，不包含令牌或凭据字段。

Compose 容器、网络、数据卷、失败构建容器、测试密钥文件、端口转发、虚拟机磁盘和临时 Docker 工具均已清理。至此最新候选链路的 7 个常规环境门禁均已显式复验，但隔离单机容器拓扑不替代目标环境的 TLS/KMS、PostgreSQL/Redis/S3 高可用端点、独立故障域、备份恢复、负载/长稳、真实迁移数据和兼容路径退役验收。Phase 6/9/10 和最终 Definition of Done 相关项继续保持未勾选。

### 15.52 Redis 独立变更死接口清理记录（2026-07-30）

数据库设计、系统测试、部署运维、单元测试和 OpenSpec 先行固定 RedisService 的活跃命令边界。全仓库精确符号与调用点审计确认 `RedisService::Expire`、`RedisService::IncrBy` 和 `TtlType` 没有生产、集成、工具、客户端或迁移消费者，只由 RedisService 直接单测覆盖。

三个无调用表面及其命令实现已删除。`RedisService_test.cpp` 通过编译期 concept 拒绝独立过期和任意步长计数回归；`RedisServiceLogContext_test.cpp` 同时拒绝旧类型、声明与实现，并锁定 8 个活跃 API 和 13 条命令事件。`Set` 继续原子写值并设置 TTL，`Incr` 继续维护文件列表世代，`CompareAndSwap` 继续旋转 refresh token，`IncrWithExpire` 继续原子管理固定窗口；上层故障策略、指标、`Result` 错误与公开 API 不变。

完整构建、Redis 聚焦 GoogleTest 14/14、Redis 命名 CTest 22/22 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 533.89 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行。Phase 10 过渡字段/配置/分支总清理项在兼容路径退役和目标环境门禁完成前继续保持未勾选。

### 15.53 分享令牌撤销死写入口清理记录（2026-07-30）

API、系统测试、部署运维、单元测试和 OpenSpec 先行固定分享撤销的读写边界。全仓库精确符号与调用点审计确认 `TokenService::RevokeShareToken` 没有生产、集成、工具、客户端或迁移消费者，仅由 `ShareAuthFilter_test.cpp` 直接调用；`VerifyShareTokenWithRedis`、`IsShareTokenRevoked`、`share_token_blacklist:{token_hash}` builder、正缓存、集成测试的精确 Redis 预置、取消后的数据库实时状态复查和安全键持久化合同仍有活跃消费者。

无调用的公开写入口及其实现已删除。撤销过滤器用例改为计算签发 token 的精确 hash 并通过 RedisService 原子写入 TTL 键，先证明本地缓存为空，再证明真实过滤器返回 HTTP 401/`40111 TokenRevoked` 并把正缓存更新为 1。TokenService 与 RedisService 源码合同共同拒绝旧方法回归，并把有生产调用的 TokenService Redis `Set` 计数从 3 收缩为 2；分享 JWT、scope、blacklist 读取、Redis 故障 fail-closed、数据库分享状态和公开响应不变。

完整构建、相关 GoogleTest 47/47、同名 CTest 47/47 和 OpenSpec 24/24 通过。首轮完整回归唯一失败是 Redis 源码合同仍保留旧的 3 次 `Set` 计数；修正为 2 后定向 1/1 通过，第二轮完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 507.15 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行。Phase 10 过渡字段/配置/分支总清理项在兼容路径退役和目标环境门禁完成前继续保持未勾选。

### 15.54 Access 撤销缓存死探针清理记录（2026-07-30）

单元测试文档与 OpenSpec 先行固定 access 撤销缓存测试边界。全仓库精确符号与调用点审计确认 `TokenService::IsRevocationCacheEntryRevokedForTest` 只有生产头文件声明、实现和同一单测中的两条调用；两条调用分别重复紧邻的缓存大小为零和一的断言，没有生产、集成、工具、客户端或迁移消费者。运行时 `IsAccessTokenRevoked`、Redis blacklist 读取、正缓存、过期清理和 access token 拒绝路径仍有独立覆盖。

冗余测试专用成员探针及两条重复断言已删除，零/一条目缓存状态断言保留；`TokenServiceLogContext_test.cpp` 的源码合同同时拒绝旧声明与实现回归。撤销缓存的插入、删除、到期语义、Redis 支持的跨实例判定和公开认证响应没有变化。

完整构建、相关 GoogleTest 34/34、同名 CTest 34/34、`AuthClusterConsistencyIntegration`/`RedisSessionPersistenceIntegration`/`AuthLifecycleIntegration` 3/3 和 OpenSpec 24/24 通过。完整 CTest 共 1429 项，1422 项通过、7 项环境门控跳过、0 失败，总耗时 501.98 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；Phase 3/10 总清理项在兼容路径退役和目标环境门禁完成前继续保持未勾选。

### 15.55 分享状态测试专用转换器清理记录（2026-07-30）

单元测试文档与 OpenSpec 先行固定分享状态 DTO 边界。全仓库精确符号与调用点审计确认 `ShareStatusToString` 只有 `ShareDto.hpp` 中的一个内联定义，以及 `ShareDto_test.cpp`/`ShareService_test.cpp` 中两组完全重复的 active/expired/cancelled 格式断言；没有生产、集成、工具、客户端或迁移消费者。生产仍直接使用 `ShareStatus` 的稳定数值完成 PostgreSQL 状态写入、过滤与授权判断，响应 DTO 直接序列化服务查询得到的既有状态字符串。

无调用转换器与六条只测死代码的断言已删除；`ShareLogContext_test.cpp` 的源码合同拒绝旧符号回归。分享状态枚举、列表参数的 all/active/expired/cancelled 校验、服务状态分支、管理/浏览/访问链路和公开 JSON status 字段均未改变。

完整构建、名称含 Share 的 GoogleTest 266/266、CTest 272/272（含 6 项真实分享集成）和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 505.04 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；Phase 10 过渡字段、配置与迁移分支总清理项在兼容路径退役和目标环境门禁完成前继续保持未勾选。

### 15.56 共享驱动项名称校验复用记录（2026-07-31）

单元测试文档与 OpenSpec 先行固定共享名称校验边界。全仓库调用点审计确认文件上传、文件重命名、文件夹创建和文件夹重命名分别维护四段同义的禁止字符循环，而 `disk::utils::HasForbiddenDriveItemChars` 已提供完全相同的 `/\\:*?\"<>|` 与 `0x00-0x1F` 判定；直接删除该辅助函数会保留生产重复实现，因此本轮将其纳入生产 DTO 边界。

四个 DTO 校验器已统一复用共享谓词并删除本地字符表与循环。`FileDtoLogContext_test.cpp` 和 `FolderLogContext_test.cpp` 分别锁定两个生产调用点并拒绝本地字符表回归；共享纯函数测试继续覆盖合法 UTF-8、跨平台保留字符和控制字节。长度、保留名称、隐藏名称、UTF-8 校验顺序、错误码、消息和公开 API 合同均未改变。

完整构建、相关 GoogleTest 68/68、`FileMutationOpsIntegration`/`FolderLifecycleIntegration`/`UploadFlowIntegration` 3/3 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 500.27 秒。目标环境的 S3、多实例、PgBouncer 和 Prometheus 门禁仍须按部署清单执行；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.57 Kubernetes Redis TLS 代理记录（2026-07-31）

能力审计确认当前 Drogon 1.9.11 及其上游 `ConfigLoader` 只支持 Redis host、port、username、password、db 与连接数，没有原生 TLS 配置入口。既有 Kubernetes 生产参考把应用直接指向私网 Redis writer 并注入密码，只覆盖认证和网络边界，不能证明传输加密；部署设计、系统测试计划和 OpenSpec 因此固定 per-Pod loopback TLS 代理边界，并明确仓库静态/语法证据不替代目标环境验收。

`deploy/kubernetes/platform` 新增共享 HAProxy ConfigMap，运行时把 Drogon Redis 固定到 `127.0.0.1:6379`。API/Worker 均增加相同 sidecar：代理只监听 loopback，以 TLS 1.2+、`verify required`、受保护 CA、SNI、`verifyhost`、5 秒连接超时、健康检查和基于 Pod `resolv.conf` 的 DNS 重解析连接私网稳定写端点。`REDIS_PASSWORD` 仍由应用用于 Redis AUTH，代理不接收该 Secret；`REDIS_CA_CERT` 只读挂载，sidecar 保持非 root、只读根文件系统、零 capability 和有界资源。发布计划要求代理镜像 digest、TLS policy 预检与目标 DNS 故障切换，基础端点和 certificate name 继续用拒绝发布占位符。

官方 HAProxy 3.2.4 源码临时构建后，仓库 ConfigMap 经环境展开通过真实 `haproxy -c`；Kustomize v5.7.1 对 platform、migration、worker、api 和稳态聚合 5/5 渲染通过。完整构建无增量工作，`DistributedTopologyContract` 1/1、OpenSpec 24/24 通过；完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 504.50 秒。Phase 6 Redis 条目继续未勾选，目标私网端点、真实 CA、证书错误拒绝、AUTH/Lua/CAS/SCAN/TTL、DNS 故障切换和 RTO/RPO 仍须现场验证；Phase 9/10 与最终 Definition of Done 也保持未勾选。

### 15.58 上传续租默认客户端死重载清理记录（2026-07-31）

数据库设计、系统测试、单元测试和 OpenSpec 先行固定上传续租仓储边界。全仓库精确调用点审计确认 `UploadTaskRepository::RenewFinalizeLease` 的五参数重载只把构造时 `m_db_client` 转发给六参数重载，没有生产、集成、工具、客户端或迁移调用；已编译主程序对象的定义/重定位差集也只把它识别为未引用的公开方法。真实完成路径在组装后和最终短事务首条业务语句前，始终显式传入 standalone client 或当前 transaction、`lease_owner` 与最新 `state_version`。

五参数声明和 17 行转发实现已删除。`UploadTaskRepository_test.cpp` 以 C++23 concept 拒绝旧调用形状、正向保留显式 `DbClientPtr` 的六参数原语，并要求仓储源码只保留一个续租定义。活跃 SQL 继续匹配 `Finalizing + lease_owner + state_version + lease_expires_at > NOW()`，使用 PostgreSQL 时间并返回递增版本；状态值、local staging 描述符、旧路径 fallback、nullable 迁移字段和 REST API 均未改变。

完整构建成功，UploadTaskRepository/UploadLifecycle 聚焦 GoogleTest 21/21、`UploadRollbackGateIntegration`/`UploadStateMachineIntegration` 2/2 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 499.16 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；Phase 3/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.59 Worker/Scheduler 准入死探针清理记录（2026-07-31）

系统测试、单元测试、部署运维和 OpenSpec 先行固定运行时准入边界。全仓库精确调用点审计确认 `ScheduledTasks::IsAccepting()` 只有声明与实现，`StorageWorkerRuntime::IsAccepting()` 除声明与实现外只有两条冗余测试断言；生产、集成、工具、客户端和迁移均没有调用，进程关停仅使用两者的 `IsDrained()`。

两个公开读探针的声明与实现、两条冗余断言已删除。Worker 与 Scheduler 单测分别以 C++23 concept 拒绝旧能力回归；Worker 仍以 drain 前 `PollOnce()` 执行回调、drain 后返回 `false` 且回调次数为 0 证明准入行为。内部 `m_accepting`、`BeginDrain`/`Stop`、`PollOnce`/`SeedOnce`、in-flight 观察、首次及周期播种和公开 API 均未改变。

完整构建、Worker/Scheduler 聚焦 GoogleTest 10/10、`SchedulerRoleCutoverIntegration`/`WorkerDrainTakeoverIntegration`/`DistributedTopologyContract` 3/3 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 512.49 秒。目标 MinIO/云 S3、PgBouncer、Prometheus 和多实例环境门控仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.60 运行时配置文件加载公开面清理记录（2026-07-31）

系统测试、部署运维、单元测试和 OpenSpec 先行固定运行时配置加载边界。全仓库精确调用点审计确认 `RuntimeConfig::LoadFile()` 只有公开声明、成员定义和同一 `.cpp` 内 `LoadFromEnvironment()` 的唯一调用；已编译对象也只有该内部重定位，没有其他生产、测试、工具、客户端或迁移消费者。

公开 `LoadFile()` 声明和成员定义已删除，原函数体不改逻辑地转为 `RuntimeConfig.cpp` 匿名命名空间的 `LoadConfigFile()`；重建后 `nm` 确认 helper 为本地 `t` 符号且旧成员符号消失。`RuntimeConfig_test.cpp` 通过 C++23 concept 拒绝旧 public 能力回归。`DISK_CONFIG_FILE` 选择、默认 `config.json`、文件打开/JSON 解析错误、单一 `default` 路由验证、环境覆盖顺序与 Drogon 加载均未改变。

完整构建、RuntimeConfig 聚焦 GoogleTest 9/9、`WorkerObservationModeIntegration`/`SecureLocalStagingCutoffIntegration`/`DistributedTopologyContract` 3/3 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 502.22 秒。目标 MinIO/云 S3、PgBouncer、Prometheus 和多实例环境门控仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.61 RuntimeConfig 单一公开管线清理记录（2026-07-31）

系统测试、部署运维、单元测试和 OpenSpec 先行固定 RuntimeConfig 单一公开管线。全仓库精确调用点审计确认 `ValidateDatabaseRouting()` 与 `ApplyEnvironmentOverrides()` 在生产中只由同一 `.cpp` 的 `LoadFromEnvironment()` 调用，其他直接消费者全部位于 `RuntimeConfig_test.cpp`；已编译对象的定义符号也确认三个组合阶段之间没有外部生产入口需求。

两个成员声明与成员定义已删除，原函数体不改逻辑地下沉到 `RuntimeConfig.cpp` 匿名命名空间。重建后 `nm` 只将 `RuntimeConfig::LoadFromEnvironment()` 标记为全局 `T` 符号，文件加载、路由验证和环境覆盖均为本地 `t` 符号。`RuntimeConfig_test.cpp` 通过三项 C++23 concept 拒绝独立公开能力回归，9 项行为测试全部改用唯一临时 JSON、`DISK_CONFIG_FILE` 和真实环境变量执行完整管线。数据库验证位于环境覆盖之前的顺序、类型/范围检查、敏感值排除、缺失目标 section 失败、Drogon 输入和启动失败语义均未改变。

完整构建、RuntimeConfig 聚焦 GoogleTest 9/9、`WorkerObservationModeIntegration`/`SecureLocalStagingCutoffIntegration`/`DistributedTopologyContract`/`AuthClusterConsistencyIntegration`/`RedisSessionPersistenceIntegration` 聚焦 CTest 5/5 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 项通过、7 项环境门控跳过、0 失败，总耗时 505.24 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；本轮不删除环境变量、配置字段、部署 Secret、schema 或迁移分支，Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.62 配额对账死公开面清理记录（2026-07-31）

系统测试、部署运维、单元测试和 OpenSpec 先行固定配额对账唯一所有权。全仓库精确调用点与已编译对象重定位审计确认 `QuotaService::GetReconciliation` 的 standalone 与 transaction-client 重载除内部转发外没有生产、测试、工具、客户端或迁移消费者，专属 `AccountingReconciliation` 仅被一条字段赋值测试使用；持久 users scope 分页扫描以及 `quota_used_mismatch`/`quota_reserved_mismatch` finding 的创建与消解已有且唯一归属 `StorageReconciliationService::RunUserPage`。

两个无调用重载、专属结构、SQL、独占 warning 和死字段测试已删除。`QuotaService_test.cpp` 通过两个 C++23 concept 与源码否定合同拒绝旧调用形状、类型、SQL 和事件回归，同时正向锁定持久对账所有者；活跃配额合同从 13 个入口/18 条事件收敛为 11 个写入入口/17 条事件。上传、复制和回收站的配额 SQL、事务连接、错误码、日志上下文与 REST 响应均未改变。

完整构建、配额/存储对账聚焦 GoogleTest 11/11、`BackupRestoreReconciliationIntegration`/`SafetyContentQuotaIntegration` 2/2 和 OpenSpec 24/24 通过。完整 CTest 共 1423 项，1416 通过、7 项按环境门控跳过、0 失败，总耗时 504.40 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.63 单项文件夹删除计划死路径清理记录（2026-07-31）

系统测试、部署运维、单元测试和 OpenSpec 先行固定文件夹删除计划唯一活跃边界。全仓库精确调用点审计确认 `disk::file::utils::FetchFolderDeletePlan` 只转发到 `FolderRepository::FetchFolderDeletePlan`，仓储方法除该转发外没有生产、测试、工具、客户端或迁移消费者；已编译对象重定位也只在 utility 包装器内指向仓储方法。`FolderService::FetchFolderSubtree` 仍有独立业务调用，FileMutation/Trash 则有三条活跃 `FetchBatchFolderDeletePlans` 路径。

utility 转发与仓储单项方法已一并删除，`FolderRepository` 从 13 个原语收敛为 12 个活跃显式 client 原语。`FolderRepository_test.cpp` 通过 C++23 concept 与源码否定合同拒绝两层旧入口回归，并正向锁定单项子树读取、三条批量计划调用、无状态仓储及连接所有权；相邻 `FileRepository_test.cpp` 同步把仓储内默认构造点从 2 调整为 1。递归用户谓词、批量覆盖消重、事务连接、回收站快照、配额/ref_count、REST 响应、schema 和迁移兼容合同未改变。

完整构建、相邻仓储聚焦 GoogleTest 6/6、`FileMutationOpsIntegration`/`FolderLifecycleIntegration`/`TrashLifecycleIntegration`/`SafetyContentQuotaIntegration`/`SafetyMoveCopyPathIntegration` 5/5 和 OpenSpec 24/24 通过。首次完整 CTest 的唯一失败是相邻文件仓储源码合同仍锁定已删除路径带来的第二个默认构造点；计数同步并定向复验后，第二轮完整 CTest 共 1423 项，1416 通过、7 项按环境门控跳过、0 失败，总耗时 508.06 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.64 单用途失败响应别名清理记录（2026-07-31）

系统测试、单元测试和 OpenSpec 先行固定进程排空拒绝响应边界。全仓库精确符号、调用点和已编译对象审计确认 `Response::Fail()` 只在 `main.cpp` 的 `BuildServiceUnavailableResponse()` 中调用，只包装 `Response::Error(ErrorInfo(code, message))`，没有其他生产、测试、工具、客户端或迁移消费者；测试二进制也没有该符号。

唯一调用点已改为显式调用 `Response::Error(ErrorInfo(...))`，单用途公开别名与不再需要的 `<string>` include 已删除。`Response_test.cpp` 以 C++23 concept 拒绝旧入口回归，`ProcessRuntime_test.cpp` 正向锁定规范错误构造、`Service is not accepting new requests`、HTTP 503 与 `Retry-After: 1`。统一 JSON 错误信封、`10001` 业务码、请求计数、排空准入、readiness、优雅退出、schema、迁移与 REST 合同未改变。

完整构建、Response/ProcessRuntime/HealthService/Worker drain 接管/分布式拓扑聚焦 CTest 23/23 和 OpenSpec 24/24 通过。重建后的后端可执行文件不再包含 `Response::Fail` 符号；完整 CTest 共 1424 项，1417 项通过、7 项按环境门控跳过、0 失败，总耗时 504.14 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.65 单用途批量输入验证器清理记录（2026-07-31）

系统测试、单元测试和 OpenSpec 先行固定分享批量取消边界。全仓库精确调用点与已编译对象审计确认 `BatchUtils::ValidateBatchInput()` 只在 `ShareService::Cancel()` 中以 `std::numeric_limits<size_t>::max()` 作为上限实例化一次，没有其他生产、测试、工具、客户端或迁移消费者。由于该上限对 `vector::size()` 恒成立，这个通用入口在生产中实际只判断分享 ID 集合是否为空。

唯一调用点已改为直接检查 `request.share_ids.empty()`，泛型验证器与不再需要的 `<limits>` include 已删除。`CleanupService_test.cpp` 以 C++23 concept 拒绝旧入口回归，`ShareLogContext_test.cpp` 正向锁定直接空集合分支和活跃 `BatchUtils::Chunk`/`BuildInPlaceholders` 路径；`BuildSafeNumericInClause` 也继续保留。服务直接调用的空成功响应、DTO 拒绝 HTTP 空数组、批量取消的分块、所有权、状态、审计、数据库副作用与 REST 合同均未改变。

完整构建、分享批量特征/清理合同/DTO/真实分享管理与审计/分布式拓扑聚焦 CTest 26/26 和 OpenSpec 24/24 通过。重建后的 `ShareService.cpp.o` 和后端可执行文件不再包含 `ValidateBatchInput` 符号；完整 CTest 共 1424 项，1417 项通过、7 项按环境门控跳过、0 失败，总耗时 507.28 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.66 上传状态机整数终态死重载清理记录（2026-07-31）

系统测试、单元测试和 OpenSpec 先行固定上传终态分类边界。全仓库精确调用点审计确认 `IsTerminalStatus(int)` 只有公开声明、实现与 `UploadStateMachine_test.cpp` 的非法值断言，没有生产、集成、工具、客户端或迁移消费者。已编译对象进一步确认生产对象没有该整数符号的未定义引用，唯一外部引用来自测试对象；反之，`StorageRecoveryAdminService` 明确消费类型化 `IsTerminalStatus(UploadTaskStatus)`，因此该生产重载必须保留。

整数重载的公开声明、五行包装实现和专用非法值断言已删除。`UploadStateMachine_test.cpp` 以 C++23 concept 拒绝整数调用形状回归并正向保留类型化能力；状态机/仓储源码合同拒绝旧声明与实现，恢复管理源码合同锁定生产类型化调用。`UploadTaskStatusFromStorage` 继续拒绝未知值，六种状态的终态分类、数值、诊断名称、finalize/cancel 决策、PostgreSQL 条件写、恢复响应与 REST 合同均未改变。

完整构建、状态机/上传仓储/恢复管理/真实 PostgreSQL 状态机与存储任务/分布式拓扑聚焦 CTest 20/20 和 OpenSpec 24/24 通过。重建后的状态机对象、后端与测试二进制不再包含 `IsTerminalStatus(int)` 符号，而类型化符号仍存在；完整 CTest 共 1424 项，1417 项通过、7 项按环境门控跳过、0 失败，总耗时 504.72 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.67 请求追踪生成/校验 helper 内部化记录（2026-07-31）

系统测试、单元测试和 OpenSpec 先行固定请求追踪公开边界。全仓库精确调用点与已编译对象审计确认 `RequestTraceFilter::GenerateRequestId()` 只由同一实现文件消费，`IsValidRequestId()` 只由该实现文件与直接单测消费，没有其他生产、集成、工具、客户端或迁移消费者。审计同时确认 `main.cpp` 的 pre-routing advice 对 `ResolveRequestId()` 有真实生产调用，`main.cpp.o` 保留该符号的未定义引用，因此解析入口必须继续公开。

生成与校验成员声明和定义已删除，原逻辑下沉为 `RequestTraceFilter.cpp` 匿名命名空间 helper；重建后的实现对象将两者标记为本地 `t` 符号，后端和测试二进制不再包含旧成员符号，公开 `ResolveRequestId()` 符号与生产链接关系继续存在。`RequestTraceFilter_test.cpp` 通过 C++23 concept 拒绝两个旧公开能力回归并正向保留解析入口，行为只经活跃解析/过滤入口验证合法上游 ID、128 字符边界、空值、超长、空格、斜杠、CR/LF 的 UUID v4 回退以及已有 `request_id` 属性不被覆盖。响应 `X-Request-Id`、结构化日志关联、进程排空请求准入、REST、schema 和迁移合同均未改变。

完整构建、请求追踪/进程准入/分布式拓扑/健康日志聚焦 CTest 16/16 和 OpenSpec 24/24 通过。完整 CTest 共 1424 项，1417 项通过、7 项按环境门控跳过、0 失败，总耗时 506.04 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.68 JWT 公开路径分类 helper 内部化记录（2026-07-31）

系统测试、单元测试、后端发现文档和 OpenSpec 先行固定 JWT 公开路径分类边界。全仓库精确调用点与已编译对象审计确认 `JwtAuthFilter::IsPublicPath()` 的唯一生产消费者是同一实现文件中的 `doFilter()`，其他直接调用仅来自两条路径单测；没有其他生产、集成、工具、客户端或迁移消费者。重建前的生产实现对象和测试对象各自携带该头文件内联弱定义，没有其他生产对象重定位。

公开成员及头文件 `<string>` 依赖已删除，原路径集合和匹配规则下沉为 `JwtAuthFilter.cpp` 匿名命名空间 helper。重建后的实现对象只保留本地 `t` 分类符号，后端与测试二进制不再包含旧成员符号，公开 `doFilter()` 生产入口保持不变。`JwtAuthFilter_test.cpp` 以 C++23 concept 拒绝旧公开能力回归；三个公开认证入口、三个健康入口、精确 `/metrics`、公开分享 access/browse/download 前缀，以及 logout、健康/metrics 额外后缀、分享 owner 和无尾随斜杠等受保护近似路径，全部改经真实过滤器入口断言放行或既有 401。`FilterOwnershipTest` 同时拒绝头文件分类规则回归，并从实现文件锁定私有路径集合及 `doFilter()` 调用关系。

首次聚焦回归的唯一失败是相邻归属合同仍从头文件读取路径字面量；同步后定向复验 1/1、完整聚焦 CTest 61/61 和 OpenSpec 24/24 通过。首次完整套件在既有 Redis 运行时用例出现连接已建立但未发命令的客户端挂起，Redis 同期可用且无 blocked client；中止后该用例定向 1/1（0.02 秒），带 300 秒默认单项上限的第二轮完整 CTest 共 1424 项，1417 项通过、7 项按环境门控跳过、0 失败，总耗时 506.96 秒。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行，PgBouncer 与 Prometheus 门控也仍待现场执行；全局 JWT 所有权、route-level 分享保护、认证属性、响应、日志、限流顺序、schema 和迁移合同均未改变，Phase 6/9/10 总项与最终 Definition of Done 继续保持未勾选。

### 15.69 GoogleTest 发现用例超时合同记录（2026-07-31）

系统测试计划、单元测试文档和 OpenSpec 先行固定 CTest 执行上限。上一批完整回归曾在 `RedisServiceRuntimeTest.SetGetExistsDeleteRoundTrip` 中出现连接已建立但未发命令的客户端无限等待；当时只能依靠命令行 300 秒默认上限重跑，仓库本身对 `gtest_discover_tests()` 产生的 1348 个独立用例没有任何 `TIMEOUT`。重新配置前直接对旧元数据执行新合同，它按预期失败并报告 1348 项中 0 项携带 60 秒上限，证明校验不会空过。

`test/CMakeLists.txt` 现在通过 `gtest_discover_tests(... PROPERTIES TIMEOUT 60)` 统一绑定单测用例上限。新增 `VerifyDiscoveredTestTimeouts.cmake` 读取构建目录生成的 `disk-test[1]_tests.cmake`，对账 `add_test()` 数与携带精确上限的 `set_tests_properties()` 数，并额外确认上述代表性 Redis 用例存在且受限。元数据不存在、没有发现用例、数量不一致或代表用例缺失都会使 `CTestDiscoveredUnitTimeoutContract` 失败。Python 集成、故障注入和环境门控用例继续使用原有独立上限，没有改变串行、跳过或目标环境证据语义。

CMake 重新配置和完整构建通过；生成元数据对账 1348/1348 项携带 60 秒上限。首次相邻聚焦的唯一失败是 `DistributedTopologyContract` 仍锁定旧的 1424/1417/7 测试库存；只同步为 1425/1418/7 后，Python 语法和拓扑脚本直接执行、代表性 Redis/超时/拓扑聚焦 CTest 3/3 以及 OpenSpec 严格校验 24/24 通过。不带命令行 `--timeout` 的最终完整 CTest 共 1425 项，1418 项通过、7 项按环境门控跳过、0 失败，总耗时 511.62 秒。当前主机无 Docker/Podman/nerdctl、Kubernetes/kind/minikube、Nginx、PgBouncer、promtool 或 MinIO/mc，也没有对应 `DISK_*` 门控变量；因此本批不构成目标高可用、TLS/KMS、独立故障域、长稳或压力验收，Phase 6/9/10 及最终 Definition of Done 继续保持未勾选。

### 15.70 存储诊断必选能力收紧记录（2026-07-31）

后端基础边界 OpenSpec 和单元测试文档先行固定存储诊断能力。活跃调用点审计确认 `HealthService` 用 staging/final inventory 进行角色就绪检查，`UploadDiagnosticService` 逐分片执行 `HeadChunkObject`，`StorageReconciliationService` 用两类 inventory 执行 staging/final 完整对账。派生类审计只有 `LocalFileStorage`、`S3ObjectStorage`、`LocalBlobStore` 和两个局部测试替身；两个生产 staging 适配器均已覆盖 HEAD 与 staging inventory，两个生产 Blob 适配器均已覆盖 final inventory。

`UploadStagingStorage` 不再为 `HeadChunkObject` 和 `ListStagingObjects` 提供返回 `InternalError` 的默认协程，`IBlobStore` 不再为 `ListFinalObjects` 提供同类 fallback；三者都改为纯虚方法，新适配器遗漏任一诊断能力将在编译期失败，而不是启动后由 readiness、管理诊断或对账任务首次发现。旧默认实现和三条“不支持”错误消息删除；Worker cleanup 与 DownloadResponder 的局部测试替身只显式补齐本用例不调用的签名。现有存储边界源码合同锁定三个 `= 0`、旧错误文本消失及 Local/S3 生产声明完整。

clang-format、完整构建、Local/S3/readiness/对账/Worker/下载聚焦 CTest 98/98 和 OpenSpec 严格校验 24/24 通过；完整 CTest 共 1425 项，1418 项通过、7 项按环境门控跳过、0 失败，总耗时 519.58 秒。当前主机无 Docker/Podman/nerdctl、Kubernetes/kind/minikube、Nginx、PgBouncer、promtool 或 MinIO/mc，也没有对应 `DISK_*` 门控变量；本批不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支，也不构成目标 HA、TLS/KMS、长稳或压力验收，因此 Phase 3/6/9/10 及最终 Definition of Done 继续保持未勾选。

### 15.71 手动清理阶段公开面收紧记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定手动清理边界。全仓声明、定义和调用审计确认，管理员控制器只调用 `CleanupService::RunExpiredCleanupOnce`；`CleanupExpiredTrash` 与 `CleanupExpiredUploadTasks` 在生产和测试中都没有类外消费者，只由组合入口按固定顺序调用。两个阶段原本公开会允许新增调用点绕过聚合结果与统一错误传播，但不是迁移、回滚或目标环境兼容入口。

`CleanupService` 现在只公开 `RunExpiredCleanupOnce`，两个阶段声明移入私有区；实现、回收站优先于上传的执行顺序、任一阶段失败时的错误传播、`CleanupRunResult` 聚合计数、日志上下文与管理员 REST 行为均未改。`CleanupService_test.cpp` 以 C++23 concept 正向要求组合入口可调用，并在编译期拒绝两个阶段入口重新成为公开能力。

clang-format、完整构建、CleanupService/分布式拓扑/真实管理员清理链路聚焦 CTest 15/15 和 OpenSpec 严格校验 24/24 通过；不带命令行 `--timeout` 的完整 CTest 共 1425 项，1418 项通过、7 项按环境门控跳过、0 失败，总耗时 508.39 秒。当前主机无 Docker/Podman/nerdctl、Kubernetes/kind/minikube、Nginx、PgBouncer、promtool 或 MinIO/mc，也没有对应 `DISK_*` 门控变量；本批不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支，也不构成目标 HA、TLS/KMS、长稳或压力验收，因此 Phase 3/6/9/10 及最终 Definition of Done 继续保持未勾选。

### 15.72 分享码查询公开面收紧记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定分享服务公开边界。全仓声明、定义和调用审计确认，`ShareService::FindShareByCode` 只由 `Access` 和失败访问处理流程在同类内部调用；分享控制器的九个业务命令不调用它，下载响应完成后只通过 `CompleteDownload` 更新统计并写审计。分享码查询 helper 不是 REST、迁移、回滚或目标环境兼容入口。

`FindShareByCode` 声明现位于 `ShareService` 首个私有区，查询实现与两个调用点未改；`CompleteDownload` 及九个控制器协作命令继续公开。`ShareLogContextContractTest` 从类声明起点截取首个公开区，负向拒绝查询 helper 重新暴露并正向要求下载完成入口存在；既有上下文调用点计数继续覆盖查询、失败计数、审计和下载更新链路。

clang-format、完整构建、分享源码合同/管理/密码保护/审计/令牌安全/下载/分布式拓扑聚焦 CTest 7/7 和 OpenSpec 严格校验 24/24 通过；不带命令行 `--timeout` 的完整 CTest 共 1425 项，1418 项通过、7 项按环境门控跳过、0 失败，总耗时 508.93 秒。当前主机无 Docker/Podman/nerdctl、Kubernetes/kind/minikube、Nginx、PgBouncer、promtool 或 MinIO/mc，也没有对应 `DISK_*` 门控变量；本批不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支，也不构成目标 HA、TLS/KMS、长稳或压力验收，因此 Phase 3/6/9/10 及最终 Definition of Done 继续保持未勾选。

### 15.73 周期播种阶段公开面收紧记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定周期任务生命周期边界。全仓声明、定义和调用审计确认，`ScheduledTasks::SeedOnce` 只有同类私有事件循环触发器 `TriggerSeed` 一个调用方；`main.cpp` 只依赖 `Initialize`、`Register`、`Stop`，关停协调只读取 `IsDrained`。播种执行阶段不是进程入口、REST、迁移、回滚或目标环境兼容能力，继续公开会允许调用方绕过正常事件循环和角色所有权。

`SeedOnce` 声明现位于 `ScheduledTasks` 私有区，定义和 `TriggerSeed` 调用点未改；初始化、注册、停止和 drain 观察继续公开。`ScheduledTasks_test.cpp` 新增 C++23 concept，在编译期拒绝该阶段重新成为公开能力。60 秒定时器、注册后的首次立即触发、持久任务去重、固定错误记录、scheduler 角色归属及停止等待语义均未改变。

clang-format、完整构建、ScheduledTasks 直接 GoogleTest 4/4、调度生命周期/角色切换/Worker 接管/分布式拓扑聚焦 CTest 9/9 和 OpenSpec 严格校验 24/24 通过；不带命令行 `--timeout` 的完整 CTest 共 1425 项，1418 项通过、7 项按环境门控跳过、0 失败，总耗时 508.03 秒。当前主机无 Docker/Podman/nerdctl、Kubernetes/kind/minikube、Nginx、PgBouncer、promtool 或 MinIO/mc，也没有对应 `DISK_*` 门控变量；环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行。本批不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支，也不构成目标 HA、TLS/KMS、长稳或压力验收，因此 Phase 3/6/9/10 及最终 Definition of Done 继续保持未勾选。

### 15.74 当前候选六项环境门禁全量复验记录（2026-07-31）

候选提交 `a3aa0452` 复用仓库固定测试依赖。MinIO `RELEASE.2025-04-22T22-12-26Z`、mc `RELEASE.2025-04-16T18-13-26Z` 与 promtool `3.13.1` 的摘要逐项匹配仓库引导合同；PgBouncer `1.25.2` 的既有临时构建重新通过版本探针并绑定二进制 SHA-256。当前机器没有 Docker/Podman/nerdctl，因此容器拓扑不具备执行条件；本批没有伪造替代容器运行结果。

六个本机可执行环境门禁先独立通过，并在同一次完整套件中再次通过：PgBouncer 事务池 3.51 秒、Prometheus 规则 0.18 秒、S3 adapter 0.37 秒、S3 应用流 11.16 秒、MinIO provisioning 2.96 秒、本地双 API/双 Worker 拓扑 140.18 秒。最后一项使用隔离 PostgreSQL、持久 Redis、固定 MinIO、两个真实 API、两个真实 Worker 与无粘性代理完成 20 项检查，覆盖严格逐请求 A/B 交替、共享认证和缓存、终态竞态、租约接管、PostgreSQL/Redis/MinIO 故障恢复以及单 API 退出后入口继续服务。

`distributed-flow-summary.json`、`s3-provisioning-summary.json`、`s3-large-promotion-summary.json` 与 `pgbouncer-transaction-pool-summary.json` 均以 `0600` 原子发布，SHA-256 分别为 `d510660413e113dfd78955ae5986df410de9e7e4ef2a3cfe1ed09d80c34adbb9`、`67658d44641e0a0fdf4bdad79d8317b617dacac674f87984971f0d14fd43a1a5`、`de0082e1c3f73c28aadda75958cbe98dcc2b582fe8c51cc362266a25f9401f4f`、`d5f1e28553e5c100c5160e65725000d5e9ce661c64e9d802d0108de17486b576`；字段审计未发现凭据、端口、路径或可重放令牌，全部受管进程与临时 MinIO 目录已清理。

带六项门禁的完整 CTest 共 1425 项，1424 项通过、仅 `DistributedFlowIntegration` 1 项按环境门控跳过、0 失败，总耗时 679.44 秒。文档更新后，Python 语法检查、拓扑合同脚本直接执行与注册 CTest 1/1、OpenSpec 严格校验 24/24 通过；合同同时对账 7 项注册库存与本次 6 项启用结果。本机进程拓扑不能证明当前候选 Dockerfile/Compose 镜像构建，也不替代目标 Nginx、TLS/KMS、PostgreSQL/Redis/S3 高可用端点、独立故障域、备份恢复、负载/长稳、真实迁移数据、兼容路径退役或预发布灰度验收，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.75 当前候选容器拓扑补充复验记录（2026-07-31）

宿主缺少 Docker/Podman 且当前内核没有可加载的 TUN 模块，因此没有把 rootless 启动失败误记为项目门禁结果。QEMU 11.0.2 的 17 个 Arch 软件包逐包通过发行签名验证，Alpine 3.24.1 virt ISO 的 SHA-256 与官方清单一致；在可用 `/dev/kvm` 上启动 8 GiB/4 vCPU 隔离来宾后，Docker Engine 29.5.3、Compose 5.1.4 和 overlay2 运行探针通过。

从提交 `03bd04de` 的工作树完整执行仓库 `Dockerfile`，vcpkg 35/35 依赖和项目 187/187 步骤通过，GNU 14.2、PostgreSQL 16.9、OpenSSL 3.6 与 AWS SDK 1.11.710 均由 CMake 实际识别。最终镜像 `disk-distributed:local` 的 ID 为 `sha256:6cfe4eab68f45d8aca850e8ec87dfa9ae6d76a6dbc3dfededa131495b058b810`、大小为 217385075 字节，入口为 `/app/disk`，运行身份为 `10001:10001`，最终环境仅保留默认 `PATH`，没有构建代理或测试凭据。

Compose 使用该镜像启动 PostgreSQL、Redis、固定 MinIO/mc、两个 API、两个 Worker 和真实 Nginx 无粘性入口。正式 `DistributedFlowIntegration` 1/1 通过（CTest 150.55 秒，总耗时 150.71 秒，20 项检查），覆盖跨 API 认证/CAS、严格交替与随机路由上传、两类终态竞态、Worker 租约接管、PostgreSQL/Redis/MinIO 原 API 进程恢复、multipart abort 重试收敛、单 API 停止后入口继续服务和跨实例即时撤销。`0600` 原子证据 `.sisyphus/evidence/distributed-flow-summary.json` 的 SHA-256 为 `4665bc999a45433b292c0fb8b1288b5584a21dcd93dd2e380492775637fb1456`，状态为 `passed`，字段审计未发现令牌、密码、密钥或凭据键。

与 15.74 的六项门禁完整 CTest 合并对账后，7 个常规环境门禁均已有当前候选证据，注册库存折算为 1425/1425 通过、0 项未执行、0 失败；该数字不等同于一次启用全部七项门禁的 1425 项完整 CTest。Compose 容器、网络、三个数据卷、测试环境文件、虚拟机磁盘和临时 Docker/QEMU/rootless 工具均已清理。环境门控用例仍须在目标 MinIO/云 S3 和多实例拓扑中执行；隔离单机容器拓扑不替代目标 TLS/KMS、高可用端点、独立故障域、备份恢复、负载/长稳、真实迁移数据、兼容路径退役或预发布灰度，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.76 上传过期单任务阶段公开面收紧记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定上传过期编排边界。全仓声明、定义和调用点审计确认，`UploadLifecycleService::ExpireInProgressUpload` 只由同类的初始化时旧任务检查与有界批量过期流程调用；`CleanupService` 与 `StorageJobWorker` 只依赖公开的 `ExpireInProgressUploads`。单任务阶段不是 REST、Worker、迁移、回滚或目标环境兼容能力，继续公开会允许新调用点绕过有界批量编排。

`ExpireInProgressUpload` 声明现已移入私有区，实现与两个内部调用点未改；`UploadLifecycleService_test.cpp` 以 C++23 concept 正向要求批量入口可调用，并在编译期拒绝单任务阶段重新公开。PostgreSQL 条件状态迁移、配额释放、持久 staging cleanup 入队、分片删除、初始化时过期检查、批量边界、错误传播与结构化日志语义均未改。

clang-format、完整构建、相关直接 GoogleTest 53/53、上传生命周期/仓储/Worker/手动清理/真实状态机/安全不变量/拓扑聚焦 CTest 57/57（120.17 秒）和 OpenSpec 严格校验 24/24 通过。不带命令行额外超时的完整 CTest 共 1425 项：1418 通过、7 项按环境门控跳过、0 失败，总耗时 507.89 秒。本批没有重跑 15.74/15.75 的带门禁环境复验，也不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.77 永久删除配额失败原子回滚记录（2026-07-31）

数据库设计、系统测试、单元测试和 OpenSpec 先行固定永久删除原子性：`users.storage_used` 扣减失败时，同一事务内的回收站删除、内容 `ref_count` 扣减与 Blob GC 入队必须全部回滚。调用链审计确认 `TrashService` 原先使用返回 `Task<void>` 的 `QuotaService::AdjustUsedStorage`；该 facade 只记录 `AdjustUsedStorageChecked` 的错误并正常返回，会让外层事务在配额写入失败后继续提交，形成文件已永久删除而用户已用空间未释放的不一致状态。

无检查 facade 的声明与实现已删除，永久删除改为调用 `AdjustUsedStorageChecked` 并把失败传播到既有事务回滚路径；配额 SQL、批量响应、成功语义与 Blob GC 协议均未改变。真实 PostgreSQL 集成测试为目标用户安装条件 trigger，强制拒绝 `storage_used` 扣减并证明该项返回稳定失败、回收站行与内容引用保留、配额和 GC 任务数不变、Blob 仍存在；移除 trigger 后重试永久删除，再证明配额释放、引用归零、GC 收敛和 Blob 删除。

clang-format、Python 语法、完整构建、相关直接 GoogleTest 73/73、配额/回收站/对账/清理/拓扑聚焦 CTest 95/95（16.04 秒）和 OpenSpec 严格校验 24/24 通过。不带命令行额外超时的完整 CTest 共 1425 项：1418 通过、7 项按环境门控跳过、0 失败，总耗时 506.15 秒。本批没有重跑 15.74/15.75 的带门禁环境复验，也不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.78 上传初始化配额/任务原子化记录（2026-07-31）

数据库设计、系统测试、单元测试和 OpenSpec 先行固定非秒传上传初始化原子性。调用链审计确认旧流程先用 standalone PostgreSQL 更新预留配额，再独立插入 `upload_tasks`；API 在两步之间退出，或任务插入失败后的 best-effort 释放再次失败时，会留下没有进行中任务归属的 `storage_reserved`。两个返回 `Task<void>` 的 `ReleaseReservedStorage` facade 又会只记录 checked 释放错误并正常返回，无法让调用方可靠感知补偿失败。

上传配置与分片数验证现先于任何配额写入完成；新任务路径通过一个 `TransactionRunner` 在同一 transaction client 上依次执行条件预留和任务插入，`UploadTaskRepository::Create` 不再隐式使用仓储默认 client，staging session 外部 I/O 仍只在数据库提交后执行。三个 init 补偿释放分支与两个 unchecked facade 已删除，取消、过期和复制继续显式处理 `ReleaseReservedStorageChecked`。真实 PostgreSQL trigger 只拒绝目标用户/文件名的任务插入，实测 HTTP 500/`10006` 与稳定消息不变，任务、used、reserved 和无归属预留量均不变；移除 trigger 后同请求重试成功，精确预留并可正常取消恢复。新静态合同在实现前按预期因两个 facade 仍公开而编译失败；首次集成运行在安装 trigger 前因新用例误写既有 helper 名称中止，修正两个调用后整套安全网通过。

clang-format、Python 语法、完整构建、相关直接 GoogleTest 27/27、内容/配额安全网 235/235、上传/配额/仓储/状态机/拓扑聚焦 CTest 32/32（133.77 秒）和 OpenSpec 严格校验 24/24 通过。不带命令行额外超时的完整 CTest 共 1425 项：1418 通过、7 项按环境门控跳过、0 失败，总耗时 513.37 秒。本批没有重跑 15.74/15.75 的带门禁环境复验，也不删除受存量任务与回滚合同保护的 `temp_path`、local staging、feature flag、schema 或迁移分支；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.79 相同哈希并发初始化单赢家记录（2026-07-31）

API、数据库、系统测试、单元测试、ADR 和 OpenSpec 先行固定相同用户/文件哈希的并发初始化语义。调用链审计确认旧流程在事务外查询 `InProgress`，随后才进入配额预留与任务插入事务；两个 API/连接可同时看到空结果，各自创建任务并重复增加 `storage_reserved`。旧查询还忽略 `Finalizing`，会在完成租约持有期间创建同哈希新任务。新的源码合同在生产实现前重新编译，并按预期因缺少事务锁、事务内复查和仍存在两条旧查询而 1/1 失败。

`UploadTaskRepository` 现以一个可复用的 `FindActiveByUserAndHash` 替代两条重复查询，同时提供显式 transaction client 重载和 `AcquireUploadInitLock`。非秒传新任务事务先按 `upload-init:<user_id>:<file_hash>` 获取 PostgreSQL `pg_advisory_xact_lock(hashtextextended(...))`，再用同一连接复查 `InProgress/Finalizing`；命中时返回已提交任务及分片进度，未命中时才预留配额并插入任务。锁随事务自动释放，不依赖进程内 mutex 或实例亲和；local `WriteChunk` 自身幂等创建分片目录，因此并发输家在创建者返回前重放任务不会引入首写目录竞态。

真实 HTTP/PostgreSQL 安全网用 8 个 barrier 同步请求初始化同一非秒传 hash，全部返回 HTTP 200/业务码 0 和同一 `upload_id`，数据库只新增一个活跃任务，reserved 仅增加一次文件大小，无归属预留量不变，取消后配额精确恢复。Python 语法、clang-format、完整构建、相关直接 GoogleTest 22/22、内容/配额安全网 245/245、上传生命周期/仓储/状态机/拓扑聚焦 CTest 32/32（133.83 秒）和 OpenSpec 严格校验 24/24 通过。不带命令行额外超时的完整 CTest 共 1425 项：1418 通过、7 项按环境门控跳过、0 失败，总耗时 504.95 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库事务合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.80 文件夹创建原子性与并发冲突记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定嵌套文件夹创建原子性与同名并发语义。调用链审计确认旧流程在事务外读取父目录并执行同名预检查，随后 standalone 插入子目录，再通过吞掉数据库异常的 `IncrementParentItemCount` best-effort 更新父目录计数；父计数更新失败会返回成功但留下计数偏差，两个并发请求也可能同时通过预检查并把唯一约束异常暴露成 HTTP 500。新的编译期/源码合同在生产实现前按预期因缺少 conflict-safe 插入、父计数仍返回 `Task<void>` 及旧吞错入口仍存在而失败。

创建路径现由一个 `TransactionRunner` 先对当前用户的父目录执行受影响行数可检查的 `item_count + 1`，借该更新取得行锁并确认父目录仍存在，再读取稳定路径/深度，最后通过 `INSERT ... ON CONFLICT (user_id, parent_id, name) DO NOTHING RETURNING` 插入子目录；缺失父目录、唯一冲突和数据库失败都会回滚计数，唯一冲突稳定映射为 `FolderAlreadyExists`。无检查的仓储 `IncrementItemCount`、服务层 `IncrementParentItemCount` 和事务外父目录 helper 已删除，既有缺失父目录结构化警告合同保留。真实 PostgreSQL trigger 拒绝目标父目录计数更新时，HTTP 500/`10006` 和稳定消息不变，子目录与父计数均不变；移除 trigger 后重试只创建一行并增加一次计数。8 个 barrier 同步同名请求恰好一个 HTTP 200，其余七个均为 `409/50010`，数据库行与父计数只增加一次。

Python 语法、差异检查、完整构建、相关直接 GoogleTest 7/7、内容/配额安全网 258/258、上传日志安全网 67/67、文件夹/变更/上传/拓扑聚焦 CTest 26/26（21.07 秒）和 OpenSpec 严格校验 24/24 通过。首次完整回归因事务化时遗漏既有缺失父目录日志标记而在 `SafetyUploadInvariantsIntegration` 失败；恢复受约束警告后该注册测试 1/1 通过（117.70 秒），最终不带命令行额外超时的完整 CTest 共 1426 项：1419 通过、7 项按环境门控跳过、0 失败，总耗时 508.73 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库事务合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.81 文件夹并发重命名单赢家记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定同父目录并发重命名的单赢家语义。调用链审计确认旧流程在事务外分别读取目标文件夹、父目录、同名冲突和子树，随后再逐项更新名称与路径；8 个 barrier 同步请求都能通过预检查，唯一约束最终使七个输家暴露为 HTTP 500/`10006`，生产日志亦记录 `uk_folders_user_parent_name` 数据库异常。新编译期/源码合同在生产实现前按预期因缺少显式 transaction client 的目标行锁、名称锁和排除自身查询而失败；旧二进制跑新安全网为 263/265，响应分布为一个业务码 0 和七个业务码 `10006`。

`FolderRepository` 新增显式 transaction client 的 `FindOwnedFolderForUpdate`、`AcquireNameLock` 和 `NameExistsExcluding`。创建与重命名现共用 `folder-name:<user_id>:<parent_id>:<name>` 的 PostgreSQL transaction advisory lock；重命名由一个 `TransactionRunner` 在同一事务中锁定目标行、锁定新名称、排除自身复查冲突、读取父目录与子树，并检查根、后代文件夹和后代文件的每一次路径写入。任一读取、冲突或受影响行数错误都走同一回滚路径；只在 `Run` 成功提交并返回后失效列表缓存。事务外 `IsFolderNameExists` 及其无用 ORM alias 已删除。带 0.5 秒 PostgreSQL `BEFORE UPDATE` 延迟 trigger 的真实安全网现为 265/265：8 个并发请求中一个 HTTP 200/业务码 0，七个稳定为 `409/50010`，目标名称与路径各只有一行，七个输家保留原名与原路径，父目录计数不变，日志不再出现该唯一约束异常。

Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 8/8、内容/配额安全网 265/265、文件夹/缓存/变更/拓扑聚焦 CTest 12 通过、1 项按 PgBouncer 环境门控跳过（19.36 秒）和 OpenSpec 严格校验 24/24 通过。首次完整回归仅因 `FileListCache_test.cpp` 仍查找旧手动 `TransactionRunner::Commit` 而 1 项失败；文档与源码合同改为检查重命名 `Run` 成功结果处理后才失效缓存，该用例 1/1 通过。最终不带命令行额外超时的完整 CTest 共 1427 项：1420 通过、7 项按环境门控跳过、0 失败，总耗时 517.46 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库事务合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.82 文件并发重命名单赢家记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定同目录文件并发重命名的单赢家语义。调用链审计确认旧流程先用 standalone client 读取目标文件、查询同名冲突和解析文件夹路径，随后再独立更新名称/扩展名/路径；8 个 barrier 同步请求都能通过预检查。手工复现得到一个 HTTP 200/业务码 0 和七个错误的 `404/50005 FileNotFound`，服务器日志记录七次 `uk_files_user_folder_name` 唯一约束异常。新编译期/源码合同在生产实现前按预期因缺少 `FindOwnedFileForUpdate`、`AcquireNameLock` 和 `NameExistsExcluding` 而编译失败；旧二进制跑新安全网为 270/273，三个失败正好对应冲突数、业务码和日志泄漏合同。

`FileRepository` 现以三个显式 transaction-client 原语锁定当前用户目标行、获取 `file-name:<user_id>:<folder_id>:<name>` 的 PostgreSQL transaction advisory lock，并排除自身查询同名冲突。文件与文件夹的名称锁前缀独立，保留两种类型允许同名的现有合同。`FileMutationService::Rename` 已改为一个 `TransactionRunner`，目标读取、名称锁、冲突检查、文件夹位置读取和最终更新均使用同一 transaction client，更新受影响行数继续检查。冲突在事务内稳定返回 `FileAlreadyExists`，缺失目标才返回 `FileNotFound`；交易成功后才记录成功并失效列表缓存。事务外且在查询失败时吞错返回 `false` 的 `IsFilenameExists` 及两个不再使用的 ORM alias 已删除。

带 0.5 秒 PostgreSQL `BEFORE UPDATE` 延迟 trigger 的真实内容/配额安全网现为 273/273：8 个并发请求中一个 HTTP 200/业务码 0，七个稳定为 `409/50007`，目标名称与路径各只有一行，七个输家保留原名与原路径，父目录计数不变，整份服务器日志不再包含该唯一约束名。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 9/9、文件仓储/变更/缓存/移动/拓扑聚焦 CTest 16 通过、1 项按 PgBouncer 环境门控跳过（23.68 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1429 项：1422 通过、7 项按环境门控跳过、0 失败，总耗时 519.46 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库事务合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.83 文件并发移动同名跳过记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定不同源目录同名文件并发移动到同一目标目录时的批量部分成功语义。调用链审计确认旧流程在事务内分批读取文件，但没有锁定文件行或目标名称；每批先查询目标占用再更新位置，多个请求可同时通过查重。带 0.5 秒 PostgreSQL `BEFORE UPDATE` 延迟 trigger 的手工旧二进制复现得到一个 HTTP 200/业务码 0 和七个 HTTP 500/`10006 Failed to move items`，服务器日志出现 13 次 `uk_files_user_folder_name`；事务回滚使一个赢家进入目标、七个输家留在源目录，目录计数仍匹配实际行。新仓储编译期合同在生产实现前按预期因缺少 `FetchOwnedFilesByIdsForUpdate` 而失败；旧二进制运行新增真实场景时 7 项通过、2 项失败，失败点精确为只有一个批量响应成功以及日志暴露唯一约束。

`FileRepository` 原批量读取入口已收紧并更名为 `FetchOwnedFilesByIdsForUpdate`，仓储仍保持 9 个活跃显式 client 原语。该入口自行排序、去重全部文件 ID，按批次和升序 `id` 使用 `FOR UPDATE` 锁定当前用户文件。`FileMutationService::Move` 在同一 `TransactionRunner` 中先锁完请求全部文件行，再收集实际跨目录项的目标名称，去重排序后依次获取重命名已使用的 `file-name:<user_id>:<target_folder_id>:<name>` transaction advisory lock；所有名称锁完成后才分批查询占用并更新。已占用名称继续按原批量合同跳过，所有请求返回成功 envelope，输家的移动计数为 0；文件位置或源/目标文件夹 `item_count` 更新为零受影响行时不再静默继续，而是回滚整个请求。提交成功后才失效文件列表缓存。

新增真实场景现为 9/9，完整内容/配额安全网为 280/280：8 个并发请求均为 HTTP 200/业务码 0，`moved_file_count` 合计恰好为 1，目标名称与路径只有一行，七个输家保留源目录与路径，目标和八个源目录的 `item_count` 均匹配实际直属项，日志不再包含该唯一约束名。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 10/10、文件仓储/变更/缓存/文件夹生命周期/移动/拓扑聚焦 CTest 17 通过、1 项按 PgBouncer 环境门控跳过（9.29 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1430 项：1423 通过、7 项按环境门控跳过、0 失败，总耗时 526.48 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库事务合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.84 文件并发复制同名锁内跳过记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定同名文件并发复制到同一目标目录时的批量部分成功、引用与配额语义。调用链审计确认旧流程在事务外查询目标名称占用并筛选候选，随后批次事务先增加 `file_contents.ref_count`、插入文件元数据并结算配额；多个请求可同时通过快照查重，正常冲突只能依靠唯一约束使整个批次回滚，再由外层释放预留配额并返回复制数 0。带 0.5 秒 PostgreSQL `BEFORE INSERT` 延迟 trigger 的手工旧二进制复现中，8 个请求均为 HTTP 200/业务码 0，复制计数合计为 1，目标只有一个副本，引用和已用配额各增加一次且无预留泄漏，但服务器日志出现 14 次 `uk_files_user_folder_name`。新源码顺序合同在生产实现前按预期因缺少事务内候选名称排序而失败；旧二进制运行新增真实场景时 9 项通过、1 项失败，唯一失败正是约束日志泄漏。

事务外名称查询继续作为无写入的快照优化，但不再承担并发裁决。每个显式文件复制批次现在进入既有 `TransactionRunner` 后，先收集候选名称、去重排序并依次获取 `FileRepository::AcquireNameLock` 的 `file-name:<user_id>:<target_folder_id>:<name>` transaction advisory lock，再用同一 transaction client 复查目标占用。只有锁内剩余项才进入引用计数递增、`InsertCopiedFiles` 和 `CommitReservedToUsed`；并发冲突项不触碰引用或 INSERT，并在同一事务通过 `ReleaseReservedStorageChecked` 释放对应预留配额。事务提交成功后才把复制数、实际大小、释放大小和 ID 映射合入公开响应，原批量成功 envelope、外层失败释放、文件夹复制和提交后缓存失效语义保持不变。

新增真实场景现为 10/10，完整内容/配额安全网为 288/288：8 个并发请求继续全部返回 HTTP 200/业务码 0，`copied_file_count` 合计恰好为 1，目标名称/内容/路径只有一行，`ref_count` 与 `storage_used` 各增加一次，`storage_reserved` 回到基线且日志不再包含该唯一约束名。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 11/11 和 OpenSpec 严格校验 24/24 通过。首次复制/配额/缓存/批量/原子性/移动/拓扑聚焦 CTest 仅因 `QuotaService_test.cpp` 仍要求旧 4 个调用而 1 项失败；该显式上下文计数更新为包含锁内释放的 5 个调用后，聚焦 CTest 29 项通过、1 项按 PgBouncer 环境门控跳过、0 失败（8.68 秒）。最终不带命令行额外超时的完整 CTest 共 1431 项：1424 通过、7 项按环境门控跳过、0 失败，总耗时 518.13 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与 PostgreSQL transaction advisory lock 合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.85 文件夹并发移动同名跳过记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定不同源父目录下同名文件夹并发移动到同一目标目录时的批量部分成功与完整子树语义。调用链审计确认旧流程虽然在一个事务内读取删除计划、查询目标占用并更新根及后代路径，却没有获取目标文件夹名称锁；多个请求可同时通过查重。带 0.5 秒 PostgreSQL `BEFORE UPDATE` 延迟 trigger 的手工旧二进制复现得到一个 HTTP 200/业务码 0 和七个 HTTP 500/`10006 Failed to move items`，服务器日志出现 14 次 `uk_folders_user_parent_name`；事务回滚使一个完整子树进入目标、七个输家留在源父目录，目录计数仍匹配实际行。新增源码顺序合同在生产实现前按预期因缺少候选文件夹名称排序而失败；旧二进制运行新增真实场景时完整安全网为 294/296，两个失败精确对应只有一个批量响应成功以及日志暴露唯一约束。

`FileMutationService::Move` 现在确定顶层移动根并收集实际跨目录项后，对候选文件夹名称去重排序，再依次获取 `FolderRepository::AcquireNameLock` 的 `folder-name:<user_id>:<target_folder_id>:<name>` PostgreSQL transaction advisory lock；全部名称锁完成后才用同一 transaction client 重新查询目标占用。已占用名称继续按既有批量合同跳过，输家不更新根位置、后代文件夹/文件路径或源/目标 `item_count`；赢家继续复用原完整子树更新流程，事务提交成功后才失效文件列表缓存。创建、重命名和移动共用同一文件夹名称锁命名空间，唯一约束只保留最终不变量，不再作为并发移动的正常 HTTP 500 分支。

新增真实场景现为 10/10，完整内容/配额安全网为 296/296：8 个并发请求均为 HTTP 200/业务码 0，`moved_folder_count` 合计恰好为 1，目标名称与根路径只有一行，七个输家保留源父目录与根路径，只有赢家的后代路径随子树更新，目标、源父目录、移动根和后代的 `item_count` 均匹配实际直属项，日志不再包含该唯一约束名。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 14/14 和 OpenSpec 严格校验 24/24 通过；文件/文件夹仓储、缓存、变更、文件夹生命周期、移动路径和拓扑聚焦 CTest 28 项通过、1 项按 PgBouncer 环境门控跳过、0 失败（27.80 秒）。最终不带命令行额外超时的完整 CTest 共 1432 项：1425 通过、7 项按环境门控跳过、0 失败，总耗时 521.72 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与 PostgreSQL transaction advisory lock 合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.86 文件夹并发复制同名锁内跳过记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定同一文件夹或不同父目录下同名文件夹并发复制到同一目标目录时的完整子树、引用与配额语义。调用链审计确认旧流程先在事务外查询目标根名称占用，随后每棵子树事务先增加内容引用，再插入根、后代文件夹与文件并更新目标计数；多个请求可同时通过快照查重，正常冲突依赖根插入唯一约束使整笔事务回滚。带 0.5 秒 PostgreSQL `BEFORE INSERT` 延迟 trigger 的手工旧二进制复现中，8 个请求均为 HTTP 200/业务码 0，只有一棵含根、子目录和文件的完整子树创建，`ref_count`、`storage_used` 与目标 `item_count` 各增加一次且无预留泄漏，但服务器日志出现 14 次 `uk_folders_user_parent_name`。新增源码顺序合同在生产实现前按预期因缺少文件夹名称锁而失败；旧二进制运行新增真实场景时完整安全网为 309/310，唯一失败正是约束日志泄漏。

每棵文件夹子树复制现在进入既有 `TransactionRunner` 后，先获取 `FolderRepository::AcquireNameLock` 的 `folder-name:<user_id>:<target_folder_id>:<root_name>` PostgreSQL transaction advisory lock，再用同一 transaction client 复查目标占用。锁内命中并发冲突时，通过 `ReleaseReservedStorageChecked` 在该事务中释放整棵子树对应预留并立即返回；输家不执行 `IncrementRefCountsChecked`、根/后代文件夹与文件插入、目标 `item_count` 更新或预留转已用。事务提交成功后才把释放大小或赢家的复制计数、实际大小与 ID 映射合入公开响应；事务外查重继续作为无写入快照优化，原批量成功 envelope 和外层失败释放语义保持不变。

新增真实场景现为 16/16，完整内容/配额安全网为 310/310：8 个并发请求均为 HTTP 200/业务码 0，复制计数合计只包含一棵根、子目录与文件均完整的子树，目标各层路径和直属计数精确，`ref_count`、`storage_used` 与目标 `item_count` 各增加一次，`storage_reserved` 回到基线且日志不再包含该唯一约束名。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 15/15 和 OpenSpec 严格校验 24/24 通过。首次配额/文件与文件夹仓储/缓存/变更/复制原子性/生命周期/移动路径/拓扑聚焦 CTest 仅因 `QuotaService_test.cpp` 仍要求旧 5 个调用而 1 项失败；显式上下文计数更新为包含文件夹锁内释放的 6 个调用后，聚焦 CTest 35 项通过、1 项按 PgBouncer 环境门控跳过、0 失败（30.26 秒）。最终不带命令行额外超时的完整 CTest 共 1433 项：1426 通过、7 项按环境门控跳过、0 失败，总耗时 515.39 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与 PostgreSQL transaction advisory lock 合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.87 同一文件夹并发移动最新父目录结算记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定同一文件夹并发移动到不同目标目录时的串行语义。调用链审计确认旧流程进入事务后先读取文件夹删除计划，直到更新根位置才取得行锁；两个请求可以同时缓存相同的原父目录，第二个请求等待第一个根更新提交后仍按旧计划继续。带 0.5 秒 PostgreSQL `BEFORE UPDATE` 延迟 trigger 的手工旧二进制复现中，两次请求均为 HTTP 200/业务码 0、各报告移动一个文件夹，但最终源父目录存储计数为 0 而实际为 1，第一个请求经过的中间目标存储计数为 1 而实际为 0，只有最终目标计数正确。新增源码顺序合同在生产实现前按预期因没有在读取删除计划前锁定请求根而失败；旧二进制运行新增真实场景时 8 项通过、1 项失败，失败精确对应两个父目录计数偏差。

`FileMutationService::Move` 现在复用已归一化并升序排列的顶层文件夹 ID，在读取任何子树删除计划前，依次通过 `FolderRepository::FindOwnedFolderForUpdate` 锁定当前用户的请求根。并发移动同一根的后续事务等待先行事务提交后，PostgreSQL `READ COMMITTED` 下的下一条删除计划查询会读取最新父目录、根路径和子树路径，再执行既有目标名称锁、冲突检查、根与后代路径更新及源/目标计数结算。两个不同目标的请求仍可依次成功，公开响应、批量部分成功语义和提交后缓存失效保持不变；每笔事务只会从其实际读取到的最新源父目录减一并向本次目标加一。

新增真实场景现为 9/9，完整内容/配额安全网为 317/317：两次并发请求均成功，最终根与后代路径对应最后提交的目标，原源目录、中间目标和最终目标的 `item_count` 全部匹配实际直属项。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 16/16 和 OpenSpec 严格校验 24/24 通过；文件/文件夹仓储、缓存、变更、生命周期、内容配额、移动路径和拓扑聚焦 CTest 30 项通过、1 项按 PgBouncer 环境门控跳过、0 失败（28.97 秒）。最终不带命令行额外超时的完整 CTest 共 1434 项：1427 通过、7 项按环境门控跳过、0 失败，总耗时 519.71 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；单 API 的真实并发证据与数据库行锁合同不替代目标多实例门禁，因此 Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.88 文件夹移动零行写入原子回滚记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定文件夹移动每一步写入的受影响行合同。调用链审计确认文件移动已检查文件位置和父目录计数更新，但文件夹移动忽略根位置、后代文件夹路径、后代文件路径以及源/目标目录计数的全部布尔结果；数据库静默抑制任一更新时，事务仍继续并返回成功。新增源码合同在生产实现前按预期因缺少首个后代目录结果检查而失败。真实 PostgreSQL `BEFORE UPDATE` trigger 对后代文件夹路径更新 `RETURN NULL` 时，旧二进制安全网为 27 项通过、8 项失败：接口错误返回 HTTP 200/业务码 0，根位置、后代文件路径和源/目标计数已经提交，仅后代文件夹保留旧路径，形成不可自洽的部分子树。

`FileMutationService::Move` 现在逐一接收并检查 `UpdateFolderLocationForMove`、`UpdateFolderPathForMove`、`UpdateFilePath` 和两次 `ApplyItemCountDelta` 的布尔结果。根或任一后代目录/文件路径、源目录计数、目标目录计数更新为 0 行时，服务记录受约束的项目类型与 ID 上下文，立即从既有 `TransactionRunner` 返回 `InternalError/Failed to move items`；事务统一回滚先前写入，只有全部步骤命中后才增加移动计数并在提交后失效缓存。参数化 SQL、用户谓词、正常批量部分成功和并发名称冲突跳过语义保持不变。

同一真实故障注入修复后为 35/35：响应稳定为 `500/10006 Failed to move items`，根父目录/路径/深度、后代目录路径/深度、后代文件路径以及源/目标 `item_count` 全部保持移动前值。Python 语法、clang-format、差异检查、完整构建、移动源码合同 6/6、内容/配额安全网 317/317 和 OpenSpec 严格校验 24/24 通过。聚焦 CTest 首轮其余 29 项通过、PgBouncer 1 项按环境门控跳过；两个安全脚本因紧邻手工 317 项运行命中共享 60 秒上传限流，窗口清空后注册用例定向复验 2/2 通过。最终不带命令行额外超时的完整 CTest 共 1435 项：1428 通过、7 项按环境门控跳过、0 失败，总耗时 524.64 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.89 文件夹复制目标计数零行回滚记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定文件夹复制目标计数的受影响行合同。调用链审计确认子树插入、内容引用递增与配额提交位于同一事务，但目标目录 `item_count + 1` 仍由服务层裸 SQL 执行且完全忽略 `affectedRows()`；该更新被静默抑制或目标行消失时，整棵副本仍会提交并计入成功响应。新增源码顺序合同在生产实现前按预期因没有调用可检查的仓储原语而失败。真实 PostgreSQL `BEFORE UPDATE` trigger 对目标目录计数更新 `RETURN NULL` 时，旧二进制内容/配额安全网为 321 项通过、7 项失败：响应错误计入两个文件夹和一个文件，目标留下完整副本，`ref_count` 与 `storage_used` 各增加一次，而目标 `item_count` 未增加；预留配额回到基线但命名空间计数已经失真。

文件夹复制事务现以显式 transaction client 调用既有 `FolderRepository::ApplyItemCountDelta`，在 `CommitReservedToUsed` 前检查返回值。目标计数更新为 0 行时，事务返回内部错误并回滚根/后代文件夹、后代文件和内容引用；外层沿用批量部分成功语义释放该子树的整笔预留，最终返回 HTTP 200/业务码 0 且 `copied_folder_count`、`copied_file_count`、`copied_count` 均为 0。服务层重复的目标计数裸 SQL 已删除，正常复制、同名冲突跳过、根目录目标和提交后缓存失效语义保持不变。

同一真实故障注入修复后内容/配额安全网为 328/328：目标路径前缀下无文件夹或文件副本，内容引用、已用/预留配额和目标 `item_count` 全部保持基线。Python 语法、clang-format、差异检查、完整构建、复制源码合同 3/3 和 OpenSpec 严格校验 24/24 通过；复制/配额/仓储/缓存/原子性/生命周期/移动路径/拓扑聚焦 CTest 37 项通过、1 项按 PgBouncer 环境门控跳过（12.19 秒）。最终不带命令行额外超时的完整 CTest 共 1436 项：1429 通过、7 项按环境门控跳过、0 失败，总耗时 517.34 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.90 文件夹复制并发目标移动路径串行记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定文件夹复制期间目标目录并发移动的路径语义。调用链审计确认旧流程只在事务外解析一次目标 `path/depth`，文件夹复制事务获取目标根名称锁后直接插入子树，直到末尾增加 `item_count` 才触碰目标行；目标移动可以在副本根插入延迟期间提交，复制随后仍用旧位置构造完整子树。新增源码顺序合同在生产实现前按预期因缺少目标 `FOR UPDATE` 行锁而失败。带 0.5 秒副本根 `BEFORE INSERT` 延迟 trigger 的旧二进制内容/配额安全网为 337 项通过、3 项失败：复制与目标移动均返回成功，目标父目录、目标路径和直属计数正确，但复制根、后代目录及后代文件三层路径全部停留在目标移动前的位置。

每棵文件夹子树复制事务现在先沿用 `folder-name` transaction advisory lock，再对非根目标调用 `FolderRepository::FindOwnedFolderForUpdate`；目标不存在时该子树事务按既有批量失败路径回滚，目标存在时从锁定模型重读最新路径与深度，之后才复查同名占用、增加引用和插入子树。若目标移动先持有根行锁，复制等待后使用其已提交新位置；若复制先持有目标行锁，移动等待复制提交，再在锁后重读计划并把新副本纳入完整路径更新。名称锁先于目标行锁的顺序继续与移动目标计数路径一致，避免为同名移动/复制引入反向等待。

同一并发场景修复后完整内容/配额安全网为 340/340：复制与移动均成功，目标、复制根、后代目录和文件路径全部位于最终层级，目标 `item_count` 匹配实际直属项。Python 语法、clang-format、差异检查、完整构建、复制源码合同 4/4 和 OpenSpec 严格校验 24/24 通过；复制/配额/仓储/缓存/原子性/生命周期/移动路径/拓扑聚焦 CTest 38 项通过、1 项按 PgBouncer 环境门控跳过（12.13 秒）。首次完整 CTest 的唯一失败是新增场景多创建 5 个目录后，后续路径安全网共享同一 60 秒文件夹限流窗口并收到 `10005`；内容/配额脚本按既有测试隔离模式在结束时清理当前用户的 `rate:folder` 键，注册顺序定向复验 2/2 通过。最终第二轮不带命令行额外超时的完整 CTest 共 1437 项：1430 通过、7 项按环境门控跳过、0 失败，总耗时 525.78 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.91 显式文件复制目标计数与路径原子性记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定显式文件复制的目标目录原子性。调用链审计确认旧文件批次事务只获取目标文件名称锁并复查冲突，随后增加内容引用、插入文件和结算配额，从未增加非根目标的 `item_count`；`InsertCopiedFiles` 也只用普通查询解析目标路径，复制插入延迟期间目标移动可提交并留下旧路径。新增源码顺序合同在生产实现前按预期失败。旧二进制内容/配额安全网为 344 项通过、6 项失败：正常复制成功但目标计数增量为 0；对目标计数更新安装 `RETURN NULL` trigger 时，复制仍报告 1 个成功项并遗留文件、内容引用和已用配额。

每个显式文件复制批次现在按名称排序取得全部 `file-name` transaction advisory lock 后，对非根目标调用 `FolderRepository::FindOwnedFolderForUpdate`，目标缺失则回滚批次；锁定目标后才复查占用、增加内容引用并插入文件，因此目标移动与复制具有确定提交顺序。实际插入完成后、配额提交前，事务以映射数量调用既有 `ApplyItemCountDelta`；返回 0 行时以内部错误回滚文件、引用、计数和已用配额，外层沿用批量部分成功语义释放整批预留并从响应计数排除该批次。根目录复制不创建虚拟计数行，同名跳过项不进入目标增量。

修复后含正常目标计数、0 行故障注入以及 0.5 秒文件插入延迟/目标并发移动的完整内容/配额安全网为 361/361：复制和移动均成功时副本路径跟随目标最终层级，目标 `item_count` 匹配实际直属项；计数更新未命中时响应计数为 0，文件、引用、已用/预留配额和目标计数全部保持基线。Python 语法、clang-format、差异检查、完整构建、复制源码合同 4/4 和 OpenSpec 严格校验 24/24 通过；复制/配额/仓储/缓存/事务/原子性/生命周期/移动路径聚焦 CTest 57 项通过、1 项按 PgBouncer 环境门控跳过（36.54 秒）。最终不带命令行额外超时的完整 CTest 共 1437 项：1430 通过、7 项按环境门控跳过、0 失败，总耗时 519.80 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.92 文件夹软删除并发子树封闭记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定文件夹软删除的并发子树封闭语义。调用链审计确认旧流程在事务外读取目录计划、构造 `folder_tree` 快照和删除 ID 列表，事务内只插入回收站、清理分享并按陈旧列表删除；`files.folder_id` 和 `folders.parent_id` 又没有父目录外键。新增源码合同在生产实现前按预期因计划未使用 transaction client 而失败。带 0.5 秒文件复制插入延迟 trigger 的旧二进制内容/配额安全网为 370 项通过、3 项失败：复制与目标目录删除均返回成功，但回收站快照缺少副本，副本活跃行继续引用已删除目录，形成孤立命名空间；内容引用仍增加一次并由孤立文件持有。

`TrashService::MoveToTrash` 现在把计划发现、行锁、快照和活跃行删除统一放入既有 `TransactionRunner`。事务反复读取请求目录的当前递归计划，按 ID 升序、每批 500 行锁定新发现的目录，直到锁后计划稳定；随后按升序锁定最终子树全部文件和其余显式文件，以锁后文件行重建每个计划的文件集合、大小和显式覆盖关系，之后才创建回收站记录、清理分享并检查文件及目录删除的精确受影响行数。并发写入先提交时进入最终快照，等待目标目录锁的复制在目录消失后由计数零行检查整体回滚；找不到任何请求项仍保留既有 `FileNotFound`，其他事务失败继续归一为 `InternalError/Failed to delete items`。批量分块、用户谓词、响应计数、软删除配额和内容引用语义保持不变。

同一并发场景修复后完整内容/配额安全网为 373/373：复制和删除均成功，副本进入目录快照并从活跃表移除，目标目录移除后不存在孤立文件，内容引用按回收站语义保留。测试脚本同时在入口和成功出口清理当前用户的 folder 限流键，避免前序注册集成测试污染 60 秒窗口。Python 语法、差异检查、完整构建、FolderRepository 与日志上下文直接 GoogleTest 9/9、OpenSpec 严格校验 24/24 通过；最终相关焦点 CTest 为 96 项通过、PgBouncer 1 项按环境门控跳过、0 失败（41.95 秒）。首轮完整 CTest 的唯一失败是重排后的既有警告日志换行不再满足源码合同；恢复原单行表达式并定向复验后，最终不带命令行额外超时的完整 CTest 共 1438 项：1431 项通过、7 项按环境门控跳过、0 失败，总耗时 522.80 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.93 单文件恢复原子消费记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定单文件恢复的原子消费语义。调用链审计确认旧流程在事务外读取回收站项和目标父目录，独立插入活跃文件后调用不检查受影响行数的 `deleteByPrimaryKey`；新增源码合同在生产实现前按预期因缺少 `FetchLifecycleRowForUpdate` 而编译失败。对目标回收站行安装 `BEFORE DELETE RETURN NULL` trigger 后，旧二进制定向场景为 10 项通过、6 项失败：首次请求仍报告成功并同时留下活跃文件与回收站项，重试自动生成 ` (1)` 名称并留下两个相同内容引用的活跃文件。

`TrashQuery` 现在接受显式 transaction client，以 `trash.id + user_id FOR UPDATE` 返回锁后的生命周期记录。`TrashService::RestoreFile` 对原名及 `name (n).ext` 候选执行有界短事务：每次重新确认并锁定文件回收站项、解析内容引用，先取得 `file-name` transaction advisory lock，再锁定仍存在的非根原父目录并读取最新路径，与文件移动/复制保持同一锁顺序；候选已占用时只回滚当前只读尝试。成功事务插入活跃文件后以用户和类型谓词精确删除回收站行，受影响行数不是 1 时整体回滚。事务提交后才写成功响应和失效列表缓存；失败批项现在统一计入 `failure_count`。恢复继续复用软删除时保留的 `content_id`，不修改 `ref_count`、`storage_used` 或 `storage_reserved`；文件夹恢复与父目录 `item_count` 的后续对称性修复不并入本批。

提交前锁顺序审查发现初版恢复先锁父目录、后锁名称，与文件移动/复制相反；收紧后的源码合同按预期红灯，再以候选名短事务改为统一的名称锁优先顺序。最终 trigger 故障注入与重试场景为 16/16，完整内容/配额安全网为 387/387：首次稳定返回失败批项与 `10006`，无活跃文件且回收站、引用和配额保持基线；移除 trigger 后重试只恢复一个原名文件并消费回收站行。Python 语法、差异检查、完整构建、回收站直接 GoogleTest 37/37 和 OpenSpec 严格校验 24/24 通过；相关聚焦 CTest 共 75 项通过、其中 PgBouncer 1 项按环境门控跳过、0 失败（32.75 秒）。最终不带命令行额外超时的完整 CTest 共 1439 项：1432 项通过、7 项按环境门控跳过、0 失败，总耗时 523.62 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.94 显式文件回收站父计数对称记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定非根目录显式文件移入回收站与恢复的父计数原子性。调用链审计确认 `MoveToTrash` 只插入回收站、清理分享并删除活跃文件，`RestoreFile` 只插入活跃文件并消费回收站，两端都未更新 `folders.item_count`。新增源码顺序合同在生产实现前按预期因缺少 `explicit_file_parent_deltas` 而失败。分别以 `BEFORE UPDATE RETURN NULL` 抑制父计数扣减和增加后，旧二进制定向场景为 10 项通过、11 项失败：两次请求都绕过 trigger 提交命名空间变化，成功软删除后父计数仍为 1，手工归零后成功恢复仍为 0。

`MoveToTrash` 现在只对未被文件夹子树覆盖的锁后显式文件按非根来源 `folder_id` 聚合负增量，在回收站批量插入后按父目录 ID 升序调用既有 `ApplyItemCountDelta`，之后才清理分享并删除活跃行；任一 0 行更新使回收站、分享、活跃文件和计数整体回滚。`RestoreFile` 的成功候选事务在活跃文件插入后、精确删除回收站行前，对已锁定的最终非根目标增加一次，0 行更新同样回滚。根目录不创建虚拟计数行，软删除期间的内容引用与配额语义不变；文件夹根的父计数以及文件夹恢复原子化继续留给后续独立提交。

修复后的双向故障注入与重试场景为 21/21，完整内容/配额安全网为 406/406：两次计数故障均返回稳定内部错误且不改变文件/回收站归属，重试后父计数按 `1 → 0 → 1` 收敛，`ref_count`、`storage_used` 和 `storage_reserved` 保持基线。Python 语法、差异检查、完整构建、相关直接 GoogleTest 46/46 和 OpenSpec 严格校验 24/24 通过；相关聚焦 CTest 共 76 项通过、其中 PgBouncer 1 项按环境门控跳过、0 失败（34.11 秒）。最终不带命令行额外超时的完整 CTest 共 1440 项：1433 项通过、7 项按环境门控跳过、0 失败，总耗时 529.04 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.95 文件夹软删除外部父计数记录（2026-07-31）

API、数据库、系统测试、单元测试和 OpenSpec 先行固定文件夹子树移入回收站时的外部父计数原子性。调用链审计确认锁后顶层删除根已用于快照和活跃子树删除，但删除事务只聚合显式文件的来源父目录，未对文件夹根的外部非根 `parent_id` 扣减直属计数。新增源码顺序合同在生产实现前按预期因缺少 `--parent_deltas[plan.root.getValueOfParentId()]` 而失败；以 `BEFORE UPDATE RETURN NULL` 抑制外部父计数扣减后，旧二进制定向场景为 9 项通过、6 项失败：请求仍返回成功、创建快照并删除活跃根及内部文件，trigger 未被触发，外部父计数仍为 1。

`MoveToTrash` 现在使用统一 `parent_deltas`：未被文件夹子树覆盖的锁后显式文件按来源 `folder_id` 聚合 `-1`，每个锁后顶层删除根按外部非根 `parent_id` 再聚合 `-1`，随后按父目录 ID 升序调用既有 `ApplyItemCountDelta`。被另一个删除根覆盖的后代和子树内部目录不逐层扣减，根目录不创建虚拟计数行；同一父目录下混合删除的文件与文件夹只执行一个合并增量。任一 0 行更新使回收站快照、分享清理、活跃文件/目录删除和全部计数变化整体回滚。文件夹恢复原子化与恢复父计数继续留给后续独立提交。

修复后的文件夹父计数故障注入与重试场景为 15/15，完整内容/配额安全网为 419/419：首次稳定返回 `500/10006` 且活跃子树、回收站和父计数保持基线，移除 trigger 后重试只创建一份快照、移除完整子树并把外部父计数从 1 扣到 0，`ref_count`、`storage_used` 和 `storage_reserved` 保持不变。Python 语法、差异检查、完整构建、相关直接 GoogleTest 69/69 和 OpenSpec 严格校验 24/24 通过；相关聚焦 CTest 共 81 项通过、其中 PgBouncer 1 项按环境门控跳过、0 失败（28.51 秒）。最终不带命令行额外超时的完整 CTest 共 1440 项：1433 项通过、7 项按环境门控跳过、0 失败，总耗时 529.23 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.96 文件夹恢复事务原子化记录（2026-07-31）

功能需求、API、数据库、系统测试、单元测试和 OpenSpec 先行统一文件夹恢复语义：有效 `folder_tree` 快照递归重建完整子树，旧数据或无有效快照时只恢复空根目录；根目录、后代目录/文件、最终非根父计数和回收站消费必须在同一事务提交。调用链审计确认旧流程在事务外读取回收站项、解析父目录和探测名称，随后用默认数据库客户端逐项自动提交目录及文件，最后调用不检查受影响行数的 `deleteByPrimaryKey`；四个名称/父目录辅助查询也全部游离在事务之外。新增源码顺序合同在生产实现前按预期因缺少 `TransactionRunner` 而失败。对目标回收站行安装 `BEFORE DELETE RETURN NULL` trigger 后，旧二进制定向场景为 18 项通过、7 项失败：请求仍报告成功并提交 2 个目录和 2 个文件，回收站项仍存在，最终父目录 `item_count` 仍为 0。

`TrashService::RestoreFolder` 现在对原名及 `name (n)` 候选执行有界短事务：每次以 `FetchLifecycleRowForUpdate` 重新确认并锁定文件夹回收站项，解析锁后快照，先取得 `folder-name` transaction advisory lock，再锁定仍存在的最终非根父目录并读取最新路径，与文件夹移动/复制保持统一锁顺序。候选未占用时，事务使用同一 client 重建快照中的根、后代目录和文件；无有效快照时只插入空根目录。全部活跃行插入完成后，对最终非根父目录执行一次 `item_count + 1`，再以用户和类型谓词精确删除回收站行并要求受影响行数为 1；任一步失败都会回滚整棵子树、父计数和回收站消费，事务提交后才写成功响应。恢复继续复用软删除保留的内容引用且不改变配额；四个事务外名称/父目录辅助方法已删除。

修复后的同一故障注入与重试场景为 25/25：首次稳定返回失败且没有任何活跃恢复行，回收站、父计数、内容引用和配额保持基线；移除 trigger 后重试只恢复一棵完整子树，把父计数从 0 增至 1 并消费回收站项。Python 语法、clang-format、差异检查、完整构建、相关直接 GoogleTest 70/70、完整内容/配额安全网 442/442 和 OpenSpec 严格校验 24/24 通过；相关聚焦 CTest 共 81 项通过、PgBouncer 1 项按环境门控跳过、0 失败（28.63 秒）。聚焦 CTest 首轮因新增场景跨过共享文件夹 API 限流窗口而失败，测试入口按既有隔离模式清理当前用户的 `rate:folder` 键后全部通过。最终不带命令行额外超时的完整 CTest 共 1441 项：1434 项通过、7 项按环境门控跳过、0 失败，总耗时 540.13 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.97 回收站 ID 转发死重载清理记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定回收站私有接口边界。全仓声明、定义和调用点审计确认，`TrashService` 中仅接收 `trash_id` 的文件/文件夹恢复与永久删除四个重载没有任何生产或测试调用；活跃 `Restore`/`Delete` 批量入口已经统一预取 `TrashLifecycleRecord`，并直接调用记录版本的事务实现。四个重载只会用默认客户端重复查询同一回收站行、手工映射记录后转发，保留了一套绕开活跃预取边界的陈旧私有路径。源码合同在删除前按预期 0/1 失败：服务体仍有 14 个日志上下文签名，四个禁止的 `uint64_t trash_id` 实现全部存在。

四个 ID 转发重载的声明、实现和不再适用的参数注释已删除，保留接口注释统一描述 `TrashLifecycleRecord` 输入。批量入口、记录预取、用户归属校验、文件与文件夹恢复事务、永久删除引用/配额事务、部分成功响应、提交后缓存失效和日志上下文传递均未改变；源码合同同时要求服务体只保留 10 个活跃日志上下文签名，并拒绝四个 ID 重载重新出现。

clang-format、差异检查、完整构建、回收站直接源码合同 7/7、回收站与清理聚焦 CTest 86/86（3.88 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1441 项：1434 项通过、7 项按环境门控跳过、0 失败，总耗时 525.68 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.98 回收站永久删除死包装器清理记录（2026-07-31）

后端低风险清理 OpenSpec 与单元测试文档先行固定回收站永久删除的唯一活跃路径。全仓调用点审计确认，接收 `TrashLifecycleRecord` 的私有 `TrashService::DeleteFile` 和 `DeleteFolder` 没有任何调用者；公开批量 `Delete` 已经内联完成预取记录的用户归属、类型和文件内容引用校验，随后直接调用 `PermanentlyDeleteTrashItems` 并映射逐项结果。两个包装器重复同一校验、永久删除调用和结果日志，已经成为不可达实现。源码合同在删除前按预期为 1/2：文件夹恢复的新结束边界合同通过，日志上下文合同因服务体仍有 10 个签名且两个禁止方法存在而失败。

两个记录版永久删除包装器的声明、实现和专属注释已删除，文件夹恢复源码合同的结束边界改为真正相邻的 `PermanentlyDeleteTrashItems`。批量 `Delete` 的逐项授权/类型/内容引用校验、错误字段与消息、部分成功计数、`freed_space` 汇总以及永久删除核心中的回收站消费、内容引用递减、GC 入队和配额释放事务均未改；源码合同要求服务体只保留 8 个活跃日志上下文签名，并拒绝两个包装器重新出现。

clang-format、差异检查、完整构建、回收站直接源码合同 7/7、回收站与清理聚焦 CTest 86/86（3.88 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1441 项：1434 项通过、7 项按环境门控跳过、0 失败，总耗时 529.93 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.99 配额服务默认客户端死状态清理记录（2026-07-31）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定配额写入的显式 client 边界。全仓调用点与已编译对象重定位审计确认，不接收 `DbClientPtr` 的 `QuotaService::ReserveStorage` 和 `ReserveUploadStorage` 两个重载没有生产、测试、工具、迁移或客户端调用；上传初始化和文件/文件夹复制的全部预留已经显式传入当前 transaction client。两个无调用重载是 `m_db_client` 字段的唯一用途，继续保留会让新调用点绕开上层事务。新增合同在实现前为 3/5：显式 client 正向合同和既有对账清理合同通过，无状态类型合同与源码合同因类型非空、不可默认构造、两个 standalone 形状、8 个默认上下文入口和默认 client 字段仍存在而失败。

两个 standalone 预留重载及 `m_db_client` 已删除，`QuotaService` 现为空类型并提供无参构造；构造期 `service=quota` 进程日志原样保留。UploadLifecycleService 的 5 个、FileMutationService 和 TrashService 各 1 个生产构造点改为默认构造，全部实际配额写入继续显式传入原 standalone client 或当前 transaction client。活跃公开面收敛为 6 个显式 client 入口；预留、释放、reserved-to-used、直接消费和已用量调整的 PostgreSQL SQL、条件行检查、零值短路、错误码、15 条操作事件、调用方日志上下文和公开响应均未改变。

clang-format、差异检查、完整构建、相关直接 GoogleTest 16/16、配额合同与完整内容/配额安全网聚焦 CTest 6/6（25.64 秒）和 OpenSpec 严格校验 24/24 通过；聚焦真实安全网继续覆盖上传初始化、复制、永久删除、引用与配额故障注入。最终不带命令行额外超时的完整 CTest 共 1441 项：1434 项通过、7 项按环境门控跳过、0 失败，总耗时 546.30 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.100 内容服务显式客户端收敛记录（2026-07-31）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定内容操作的显式 client 边界。全仓调用点与已编译对象重定位审计确认，`ContentService` 只有 standalone `FindByMd5` 仍隐式使用构造期客户端，其两个生产调用者都已持有同一 `m_db_client`；其余查找、引用变更和 GC 入队入口已显式接收 standalone 或 transaction client。成员客户端还被用于构造 reference gate 与 Blob GC 入队仓储，尽管随后实际操作传入的是另一个显式 client，因而留下了可能错配事务归属的伪依赖。新增合同在实现前为 2/4：活跃内容边界和不变元数据用例通过，无状态类型与源码合同因非空类型、不可默认构造、standalone 查找、默认字段、隐式仓储构造及 6 个注入式服务构造点而失败。

standalone MD5 查找重载与 `m_db_client` 已删除，`ContentService` 现为空类型并提供无参构造；构造期 `service=content` 进程日志原样保留。UploadLifecycleService 的 3 个、FileMutationService、ShareService 和 TrashService 各 1 个生产构造点改为默认构造；两个 MD5 查找显式传入原 `m_db_client`，reference gate 和 Blob GC 入队仓储改为绑定方法收到的同一操作 client。活跃公开面收敛为 6 个显式 client 入口；参数化 SQL、行锁、引用计数不变量、reference gate、GC 入队语义、9 条失败事件、调用方日志上下文和公开响应均未改变。

clang-format、差异检查、完整构建、直接 ContentService GoogleTest 4/4、内容合同与完整内容/配额安全网聚焦 CTest 5/5（25.73 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1441 项：1434 项通过、7 项按环境门控跳过、0 失败，总耗时 529.24 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.101 系统信息服务伪输入清理记录（2026-07-31）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定系统信息服务的最小输入边界。全仓源码与已编译对象审计确认，`SystemService::GetInfo` 的 `user_id` 只被 Controller 读取并传入，服务实现从未使用；构造器注入的 Redis client 也只保存到 `m_redis_client`，没有任何读取点，响应中的 Redis 连接池大小始终由 `ConfigMgr` 提供。Controller 对 JWT 写入的 `user_id` 属性存在性检查与 `TokenMissing` 分支是真实认证门禁，不属于可删输入。新增合同在实现前为 0/2：context-only 服务形状、无 user-scoped 形状、无 Redis 依赖与 Controller 精确调用均按预期失败，而认证属性和错误码正向合同保持通过。

`SystemService` 构造器现只接收存储统计 SQL 必需的 DB client，Redis include、构造参数和成员状态已删除；`GetInfo` 只按值接收可选 `LogContext`。`SystemController` 不再注入 `getRedisClient()`，也不再读取/传递 user ID 值，但仍在任何服务调用前检查 `user_id` 属性并保留原 `TokenMissing` 响应。精确路由、认证语义、DB 查询、配置池大小、uptime/版本/聚合统计、`system_get_info` StageTimer、错误日志与公开响应均未改变。

clang-format、差异检查、完整构建、直接 SystemService/LogContext GoogleTest 5/5、系统服务合同与真实系统信息认证/响应聚焦 CTest 6/6（3.52 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1442 项：1435 项通过、7 项按环境门控跳过、0 失败，总耗时 547.05 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.102 操作日志冗余读取状态清理记录（2026-07-31）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定当前用户操作日志的最小读取模型。全仓字段读写与 SQL 审计确认，`OperationLogItem::user_id` 只在分页查询映射时被赋值，`ToJson()` 和其他生产、测试调用从不读取或公开它；计数与分页查询已分别使用参数化 `WHERE user_id = $1` 限定当前认证用户，因此再次投影并保存同一 ID 不提供授权或响应价值。新增合同在旧实现上按预期 0/1 失败，分别检出该成员、SELECT 投影和行赋值仍存在，而 JSON 排除与两处用户谓词正向合同保持通过。

`OperationLogItem::user_id`、分页 SELECT 的 `user_id` 列和结果行赋值已删除。两个查询继续使用原 user ID 参数与 PostgreSQL 占位符，计数、排序、LIMIT/OFFSET、空值映射、错误日志和 `Result` 均未改变；`operation_logs` schema、用户过滤、五个领域审计写入所有者、已存审计内容和公开 JSON 响应也保持不变。源码合同同时锁定读取模型无该成员、分页 SQL 不再投影它、行映射不再赋值，并要求两个查询的用户谓词继续存在。

clang-format、差异检查、后端与测试目标完整构建、直接 OperationLog GoogleTest 5/5、操作日志合同与真实分页/日志安全网聚焦 CTest 6/6（118.81 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1443 项：1436 项通过、7 项按环境门控跳过、0 失败，总耗时 524.65 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.103 Worker 运行时实例伪输入清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定 Worker 运行时的最小构造边界。全仓构造点与数据流审计确认，`StorageWorkerRuntime` 的 `instance_id` 参数只在构造期检查长度，之后既不保存也不参与轮询、排空、日志或关停；同一配置值已由先行构造的 `StorageJobWorker` 独立校验、保存并用于任务认领、续租和结果回写的 lease owner，`ScheduledTasks` 也独立持有实际播种身份。新增构造能力合同在旧实现上按预期 0/1 失败：回调/选项形状不可构造，而旧实例 ID/回调/选项形状仍可构造。

`StorageWorkerRuntime` 构造器现只接收 `RunCallback` 与可选轮询配置，重复的实例参数、长度校验和不再需要的 `<string>` include 已删除；`main` 的唯一生产构造点不再向运行时重复传入实例 ID，单元夹具同步使用最小形状。`StorageJobWorker` 的实例参数、1 至 128 字符校验、`m_instance_id` 与全部仓储租约调用均未修改，ConfigMgr/ProcessRuntime 的实例校验、Scheduler 身份、`Logger::SetInstanceId`、回调必选和轮询间隔校验也保持不变。构造能力合同正向锁定新形状并拒绝旧伪输入回归。

clang-format、差异检查、后端与测试目标完整构建、直接 StorageWorkerRuntime GoogleTest 7/7、运行时/进程身份/角色切换/真实排空接管/拓扑聚焦 CTest 19/19（35.29 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1444 项：1437 项通过、7 项按环境门控跳过、0 失败，总耗时 523.65 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.104 完成认领冗余诊断投影清理记录（2026-08-01）

数据库设计、单元测试和后端低风险清理 OpenSpec 先行固定完成认领的最小返回边界。全仓字段读写与 SQL 审计确认，`FinalizeClaimResult::finalize_attempts` 只在认领成功和 CAS 未命中两条查询路径中赋值，`UploadLifecycleService` 及其他生产、测试、工具和迁移调用从不读取它；数据库列本身仍由认领/接管 CAS 原子递增，并被故障测试和运维诊断直接读取。新增合同在旧实现上按预期 0/1 失败，分别检出结果成员、两条查询投影和两处映射仍存在，而 SQL 自增正向合同保持通过。

`FinalizeClaimResult::finalize_attempts`、认领成功的 `RETURNING` 投影、CAS 未命中的 `SELECT` 投影及两处结果映射已删除。数据库 `upload_tasks.finalize_attempts` 列、非负约束、迁移兼容、认领与接管原子自增、运维查询和真实故障断言均未改变；认领结果继续返回 disposition、`state_version` 与完成重放所需的 `completed_file_id`，分片完整性、PostgreSQL 时间、owner/version 租约和全部完成状态分支保持原语义。源码合同同时拒绝冗余投影回归并正向锁定持久计数自增。

clang-format、差异检查、后端与测试目标完整构建、直接 UploadTaskRepository GoogleTest 12/12、状态机/仓储/真实上传安全网聚焦 CTest 19/19（119.91 秒）和 OpenSpec 严格校验 24/24 通过。首轮完整 CTest 共 1445 项，唯一失败为 `ContractReadinessCycleIntegration` 的数据库观察时长略低于配置 TTL；该用例随后定向复验 1/1 通过（7.82 秒）。第二轮不带命令行额外超时的完整 CTest 共 1445 项：1438 项通过、7 项按环境门控跳过、0 失败，总耗时 523.84 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.105 清理记录重复旧路径投影清理记录（2026-08-01）

数据库设计、单元测试和后端低风险清理 OpenSpec 先行固定上传清理描述符与旧 local 路径兼容边界。全仓字段与查询审计确认，`UploadTaskCleanupRecord::temp_path` 只在通用行映射器中赋值，取消、单项过期和批量过期流程均不读取它；三条清理查询已经在 SQL 内通过 `COALESCE(staging_prefix, temp_path) AS staging_prefix` 生成实际消费的统一描述符。新增合同在旧实现上按预期 0/1 失败，分别检出成员、映射、两条 `RETURNING` 和一条 `SELECT` 的独立投影，而四处兼容 fallback 数量合同保持通过。

`UploadTaskCleanupRecord::temp_path`、通用映射赋值和三条清理查询的独立 `temp_path` 投影已删除。`upload_tasks.temp_path` 数据库列、新任务写入、V002/V003 混跑字段、`FindStagingSessionForUser` 及三条清理查询中的全部四处 `COALESCE` 兼容读取均未改变；取消和过期流程继续消费同一 backend/prefix 描述符，释放原预留配额、入队持久 cleanup、删除分片并传播状态版本。源码合同同时拒绝重复状态回归，并锁定两条最小 `RETURNING`、一条最小 `SELECT` 和四处 legacy fallback。

clang-format、差异检查、后端与测试目标完整构建、直接 UploadTaskRepository GoogleTest 13/13、仓储/灰度/local 迁移/contract-readiness/真实状态机与上传安全网聚焦 CTest 19/19（152.84 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1446 项：1439 项通过、7 项按环境门控跳过、0 失败，总耗时 525.12 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.106 周期播种器实例伪输入清理记录（2026-08-01）

部署运维、系统测试、单元测试和后端低风险清理 OpenSpec 先行纠正周期播种身份职责。全仓调用点与实现数据流审计确认，`ScheduledTasks::Initialize` 的 `instance_id` 只在入口检查 1 至 128 字符，之后既不保存也不参与播种计划、去重、日志、准入或关停；唯一生产调用发生在 `main`，同一配置身份此前已由 `ConfigMgr` 解析校验、由 `ProcessRuntime` 保存，并由 `StorageJobWorker` 独立校验和持有以执行真实 lease owner 操作。新增初始化能力合同在旧实现上按预期 0/1 失败，新/旧调用形状、声明、实现、重复校验和 `main` 调用共命中 7 个失败点。

`ScheduledTasks::Initialize` 现只接收播种所需的数据库客户端，重复的实例参数和长度校验已删除，`main` 唯一调用点同步使用最小形状。数据库客户端必选校验、单例幂等初始化、首次立即播种、60 秒周期、六任务 UTC 计划、PostgreSQL dedupe key、角色准入、停止接收和 drain 观察均未改变；持久任务租约身份继续由 `StorageJobWorker` 持有，顶层日志实例继续来自 `Logger::SetInstanceId`。文档不再把播种去重误述为 Scheduler 独立身份，构造能力与源码合同正向锁定数据库单参数形状并拒绝旧伪输入回归。

clang-format、差异检查、后端与测试目标完整构建、直接 ScheduledTasks GoogleTest 5/5、Scheduler/Worker 运行时/进程身份/真实角色切换/contract-readiness/排空接管/拓扑聚焦 CTest 25/25（43.18 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1447 项：1440 项通过、7 项按环境门控跳过、0 失败，总耗时 525.46 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.107 应用组合上下文重复依赖状态清理记录（2026-08-01）

ADR、单元测试文档和后端低风险清理 OpenSpec 先行固定应用组合上下文的最小持有边界。全仓成员读写与初始化数据流审计确认，`ApplicationContext` 的 DB、Redis 和上传暂存成员只在 `initialize()` 中被赋值并用于紧接着构造服务，初始化完成后没有访问器或其他生产路径再次读取；Blob store 则仍由文件和分享控制器通过上下文访问，必须保留。新增源码合同在旧实现上按预期为 0/1，共检出三个重复成员和三条仍经成员转发的服务接线失败；Blob store 保留合同通过。

`ApplicationContext` 已删除 `m_db_client`、`m_redis_client` 和 `m_upload_staging_storage`，并把入口参数直接传给下载完整性、上传、文件查询、文件变更、文件夹、分享和清理服务；各服务仍按原构造器取得相同 shared DB/Redis client 或暂存指针。`m_blob_store`、`BlobStore()` 访问器、公开初始化签名、幂等初始化、JWT secret 移交、fallback 装配、controller 服务单例和启动顺序均未改变，因此不会缩短实际服务依赖的生命周期或改变 HTTP 行为。

clang-format、差异检查、后端与测试目标完整构建、应用组合/存储能力/工厂直接 GoogleTest 5/5、核心领域/上传/变更/文件夹/分享/回收站真实服务装配聚焦 CTest 8/8（17.17 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，总耗时 526.75 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.108 文件仓储非锁定单项读取死原语清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定文件仓储的最小读取边界。全仓精确符号、调用点和已编译对象审计确认，`FileRepository::FindOwnedFile` 只有声明与定义，`kSelectOwnedFileSql` 也只由该方法引用；生产、集成、工具、客户端和迁移均没有调用者，两处测试命中只是正向要求孤立接口和 SQL 存在。写路径已经统一使用同一事务的 `FindOwnedFileForUpdate`，只读详情与下载路径则直接用 `id + user_id` ORM Criteria。新合同在旧实现上按预期为 0/2，共检出活跃显式 client 数量、声明、定义、专属常量和独占 SQL 六个失败点，事务内替代原语合同保持通过。

`FileRepository::FindOwnedFile`、专属 `kSelectOwnedFileSql` 及两个过时正向断言已删除，仓储公开面从 9 个收敛为 8 个活跃显式 client 原语。源码合同同时拒绝声明、定义、常量和无锁 SQL 回归，并正向锁定 `FindOwnedFileForUpdate` 及其用户谓词和 `FOR UPDATE`。文件重命名、移动、复制、文件夹路径更新、回收站子树锁定继续使用原 transaction client；文件详情、下载信息和下载数据继续通过 ORM 的文件 ID 与用户 ID 双 Criteria 查询，公开错误与响应不变。数据库 schema、迁移兼容字段和持久数据均未修改。

clang-format、差异检查、后端与测试目标完整构建、直接 FileRepository GoogleTest 6/6、文件仓储/变更/缓存/文件夹/回收站/配额/路径安全网聚焦 CTest 26/26（39.94 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，总耗时 542.50 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.109 批量文件夹删除计划重复转发清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定批量文件夹删除计划的唯一持久化边界。全仓调用点和源码审计确认，`disk::file::utils::FetchBatchFolderDeletePlans()` 只默认构造 `FolderRepository`，再原样转发 client、目录 ID 集合和用户 ID；`FileMutationService` 已持有仓储成员，`TrashService` 也已在删除事务体内创建仓储实例，没有独立 utility 行为、日志、错误映射或兼容消费者。新增源码合同在旧实现上按预期为 0/1，精确检出 utility 声明、实现、第二个 utility 仓储构造以及 FileMutation/Trash 未直接调用仓储五个失败点。

utility 声明与实现已删除；文件复制改用既有 `m_folder_repository` 和 standalone `m_db_client`，文件夹移动继续使用该成员与当前 transaction，移入回收站则使用事务体内既有 `folder_repository` 与同一 transaction。`FolderRepository` 继续保留 16 个活跃显式 client 原语，utility 内仓储构造从 2 个收敛为只服务目录定位的 1 个。批量 SQL、用户谓词、目录覆盖消重、循环内计划稳定化、锁顺序、快照、配额、ref_count、日志上下文、REST 响应、schema 与迁移兼容合同均未改变。

clang-format、差异检查、后端与测试目标完整构建、直接 FolderRepository GoogleTest 8/8、目录仓储/文件变更/复制删除/目录与回收站生命周期/配额/路径安全网聚焦 CTest 26/26（39.98 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，总耗时 548.70 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.110 文件夹定位重复转发清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定文件夹定位的唯一持久化边界。全仓调用点、源码与已编译对象审计确认，`disk::file::utils::ResolveFolderLocation()` 只把同一 client、目录 ID、用户 ID 和 `LogContext` 原样转发给 `FolderRepository::ResolveOwnedFolderLocation()`；两个 FileMutation 调用点已有 `m_folder_repository`，两个 UploadLifecycle 调用点所在事务也已有用于父计数的局部仓储，没有独立 utility 日志、错误映射、工具、客户端、迁移或兼容消费者。新增源码合同在旧实现上按预期为 0/2，共检出接口、实现、utility 仓储构造、日志上下文入口计数、四处未直接调用和四处旧调用残留 10 个失败点。

utility 声明、实现和 `FileServiceUtils.cpp` 不再需要的仓储 include 已删除；FileMutation 的两处调用改用既有成员，UploadLifecycle 的两处局部仓储声明前移并复用于目录定位与条件父计数更新。生产目录定位现统一为 FileMutation 四处、UploadLifecycle 两处共六个直接仓储调用，全部继续传入原 standalone client 或当前 transaction 与原 `LogContext`；FileServiceUtils 的可选日志上下文入口从 4 个收敛为三个真正拥有写失败日志的批量写入入口。目录查询 SQL、用户谓词、固定 warning、错误传播、文件路径、父计数、上传状态、配额、ref_count、REST 响应、schema 与迁移兼容合同均未改变。

clang-format、差异检查、后端与测试目标完整构建、直接 FolderRepository GoogleTest 8/8、上传生命周期/上传仓储/目录仓储/文件变更/上传与配额路径安全网聚焦 CTest 51/51（159.96 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，总耗时 527.87 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.111 回收站批量插入重复服务转发清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定回收站批量插入的唯一持久化边界。全仓调用点、源码与已编译对象审计确认，公开 `TrashService::CreateTrashRecords()` 只把同一 client、批量项目、用户 ID 和 `LogContext` 原样转发给 `disk::file::utils::InsertTrashRecords()`；除 `MoveToTrash()` 的单一内部调用外没有生产、集成、工具、客户端或迁移消费者。新增源码合同在旧实现上按预期为 0/3，共检出两个事务顺序合同缺少直接调用，以及服务声明、定义和旧内部调用五个失败点。

服务声明与实现已删除，`MoveToTrash()` 在原事务位置直接调用保留的持久化 helper。插入仍位于快照构造之后、父计数更新与活跃行删除之前，继续使用同一 transaction、`trash_items`、`user_id` 和 `log_context`；`InsertTrashRecords` 的参数化批量 SQL、固定 warning、bool 结果、失败映射、事务回滚、父计数、分享清理、ref_count、配额、缓存、REST 响应、schema 和迁移兼容合同均未改变。

clang-format、差异检查、后端与测试目标完整构建、直接 FolderRepository/TrashQuery GoogleTest 16/16、回收站/文件变更/删除回归/内容与配额/路径安全网聚焦 CTest 36/36（44.47 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 日志覆盖 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，逐项耗时合计 523.13 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.112 上传配额预留重复服务转发清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定配额预留的唯一写入原语。全仓调用点、源码、已编译对象与 Git 历史审计确认，显式 client 版 `QuotaService::ReserveUploadStorage()` 只把同一 client、用户 ID、字节数和 `LogContext` 原样转发给 `ReserveStorage()`；除非秒传上传初始化的单一生产调用外，没有集成、工具、客户端或迁移消费者，对象重定位也只来自同一生产调用的应用与测试编译产物。新增源码合同在旧实现上按预期为 0/3，共检出公开形状、上下文入口数量、声明、定义、转发和上传事务直接调用/顺序共 7 个失败断言。

该服务声明与实现已删除，上传初始化在原 `TransactionRunner` 事务位置直接调用 `ReserveStorage()`。调用仍位于获取 transaction advisory lock 和锁内 `InProgress/Finalizing` 复查之后、上传任务创建之前，继续传入同一 transaction、`command.user_id`、`command.file_size` 和 `log_context`。通用原语的零字节短路、PostgreSQL 条件 UPDATE、`affectedRows()` 判定、固定日志、checked `Result`、配额不足错误、事务回滚、上传状态、缓存、REST 响应、schema 和迁移兼容合同均未改变。

clang-format、差异检查、后端与测试目标完整构建、直接 QuotaService/UploadTaskRepository GoogleTest 18/18、配额/上传生命周期/上传仓储/文件变更/复制删除/回收站/安全网聚焦 CTest 46/46（153.22 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1448 项：1441 项通过、7 项按环境门控跳过、0 失败，总耗时 532.20 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.113 用户限流器重复固定窗口 helper 清理记录（2026-08-01）

系统测试、部署运维、单元测试和后端低风险清理 OpenSpec 先行固定用户限流固定窗口计算的唯一共享边界。全仓调用点、源码与已编译对象审计确认，API、上传、下载、文件夹、管理员和注册六个限流器头文件中的私有 `GetCurrentWindow()`/`GetResetTime()` 共 12 个成员只有定义，没有调用点或编译符号；六个实现已经各自唯一调用 `RateLimitHelper` 的 `GetFixedWindowStart()`/`GetFixedWindowReset()`，而六个 `<chrono>` include 只服务死成员。新增集中源码合同在旧实现上按预期为 0/1，共精确检出 18 个失败断言：六个冗余 include、六个起点 helper 和六个重置 helper。

六个头文件的 12 个私有死成员及六个冗余 `<chrono>` include 已删除；`DEFAULT_LIMIT`、`WINDOW_SECONDS`、可注入计数器和所有 `.cpp` 调用均未修改。共享 helper 继续接收每个实现的配置或默认窗口秒数，Redis key 窗口起点、首次递增 TTL、限额、`X-RateLimit-*`、`Retry-After`、日志上下文、fail-open、认证顺序、路由归属、响应和副作用合同均未改变；`ShareRateLimitFilter` 原本只使用共享 helper，不增加兼容分支。

clang-format、差异检查、后端与测试目标完整构建、直接全限流 GoogleTest 98/98、限流/下载/文件夹/存储任务/回收站/上传安全网聚焦 CTest 106/106（155.57 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1449 项：1442 项通过、7 项按环境门控跳过、0 失败，总耗时 527.03 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.114 活跃文件扩展名解析重复实现清理记录（2026-08-01）

数据库、系统测试、单元测试和后端低风险清理 OpenSpec 先行固定活跃文件元数据扩展名解析的唯一共享边界。全仓调用点、源码、已编译对象与 Git 历史审计确认，`UploadLifecycleService.cpp` 的匿名 `ExtractExtension()` 与 `FileMutationService::ExtractExtension()` 实现完全相同：均在最后一个 `.` 之后提取原样后缀，无点或点位于末尾时返回空字符串。前者服务上传秒传和完成的两个元数据写入点，后者服务文件重命名的一个写入点，两个实现文件已经依赖 `FileServiceUtils`。新增集中源码合同在旧实现上按预期为 0/1，共精确检出共享声明/实现缺失、三个调用点未迁移和两个本地副本残留 7 个失败断言。

`disk::file::utils::ExtractFileExtension()` 已作为单一共享纯函数加入，三个生产写入点均直接调用它；上传匿名副本与文件变更类的私有声明/实现已删除。直接行为测试锁定无点、末尾点、单点、多点、大小写原样和既有点前缀输入语义。文件名 DTO 校验、名称、路径、数据库字段、事务 client、写入顺序、错误、响应和副作用均未修改；`TrashService` 的专用解析还需识别恢复冲突名 `name (n).ext`，因此保持独立，不引入兼容分支或行为合并。

clang-format、差异检查、后端与测试目标完整构建、直接上传生命周期/上传仓储/文件与文件夹仓储/文件变更 GoogleTest 51/51、相关真实流程与三组安全网聚焦 CTest 58/58（156.79 秒）和 OpenSpec 严格校验 24/24 通过。最终不带命令行额外超时的完整 CTest 共 1451 项：1444 项通过、7 项按环境门控跳过、0 失败，总耗时 525.88 秒。本批没有重跑 15.74/15.75 的带门禁双 API 环境复验；Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.115 当前候选六项环境门禁与 Redis 隔离复验记录（2026-08-01）

系统测试、单元测试文档与可观测性 OpenSpec 先行固定 S3 应用门禁的 Redis 隔离和回收边界。候选基线 `4d76b9d9` 上，固定 MinIO `RELEASE.2025-04-22T22-12-26Z`、mc `RELEASE.2025-04-16T18-13-26Z`、promtool `3.13.1` 和 PgBouncer `1.25.2` 的版本与摘要均匹配仓库合同，六个本机环境门禁独立运行 6/6 通过。首轮带六项门禁的完整 CTest 在前 1447 项通过后，`S3AppFlowIntegration` 的故障上传 init 被共享 Redis 中同一用户累计到 241 的上传限流计数拦截为 429；该门禁单独运行成功，因此确认是测试夹具共享 Redis 的顺序污染，而不是 S3 业务回退。

`test_s3_app_flow.py` 现在解析显式 `DISK_REDIS_SERVER_BIN` 或本机 Valkey/Redis，在随机端口启动无持久化的测试自有实例，并把生成配置和 `REDIS_HOST/PORT/DB` 环境覆盖同时指向该实例。所有退出路径先停止 Disk，再终止并必要时杀死自有 Redis；不连接、`FLUSHDB` 或删除共享 Redis 键。`DistributedTopologyContract` 静态锁定自有实例、配置/环境覆盖、回收和两类禁止清场操作；S3 唯一前缀、PostgreSQL 记录清理、分片/promote/下载/持久 cleanup/故障保留业务断言均未改。

修复后 Python AST 语法、差异检查、拓扑合同直接执行、`S3AppFlowIntegration` 定向 1/1（11.45 秒）和 OpenSpec 严格校验 24/24 通过。同序完整复验共 1451 项：1450 项通过、仅缺少 Docker 的 `DistributedFlowIntegration` 1 项按环境门控跳过、0 失败，总耗时 683.59 秒；其中 PgBouncer、promtool、S3 adapter、S3 应用流、provisioning 和本地双 API/双 Worker 拓扑分别为 3.53、0.12、0.39、11.20、2.84 和 140.26 秒。四份 `0600` 原子证据摘要分别为 `2aca3c415def54406ef4c841ecc4a5e21b6d61495858a5a740af38dc2f7ce2d5`、`bac4eb1e62d233f6e4a78c42041003d43787e93ff2f204647d038258408868c1`、`a2b40ff98172805f8bf5ba379df40bacacec9c96c4a5fde1f2dde2b6bfd3dc9f` 和 `1433b80f6ad5a21b258c05573c38c8175ff1002678642e900091d5c61cd34ca8`，字段审计未发现实际凭据、端口、路径或可重放令牌；受管进程、`18080/19000/19001` 监听与本批临时目录已清理。本机进程拓扑不替代当前候选 Dockerfile/Compose 或目标 TLS/KMS、HA、故障域、备份恢复、长稳/压力、真实迁移和预发布验收，Phase 3/6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.116 固定 PgBouncer 测试二进制引导记录（2026-08-01）

系统测试、部署、单元测试文档与验证 OpenSpec 先行固定 PgBouncer 门禁依赖合同。新增 `scripts/fetch-pgbouncer-test-binary.sh` 只从 pgbouncer.org 版本化 HTTPS URL 下载 `1.25.2` release tarball，并在解包前核对官方 SHA-256 `924ad35113fd0a71c8e2dbe85b5d03445532e2b7b37a9f8a48983beea238b332` 和单一顶层目录。脚本先拒绝已有符号链接、非普通文件、不可执行或版本不符输出，再检查 C 编译器、libevent、c-ares 与 OpenSSL；构建结果首行精确返回 `PgBouncer 1.25.2` 后才以硬链接非覆盖发布 `0755` 普通文件，下载与私有构建目录在所有退出路径回收。拓扑合同锁定固定 URL/摘要、依赖、路径边界、版本探针、非覆盖发布和不匹配文件原字节不变。

全新临时目录的真实首次下载/构建和二次复用均通过，目录只保留 `pgbouncer`；本机产物 SHA-256 为 `333e81fd2a6bd35bf5815be21d40830858beace30e06c3c08a3421013c25f34c`，版本为 `1.25.2`，链接 libevent `2.1.13`、c-ares `1.34.8` 和 OpenSSL `3.6.3`。该产物驱动 `PgBouncerTransactionPoolIntegration` 1/1 通过（3.52 秒）；`0600` 证据 `.sisyphus/evidence/pgbouncer-transaction-pool-summary.json` 的 SHA-256 为 `879f914c29015cfadb00020b0a46e415abf2ba6b8e98788b719e498cd8d239f1`，字段审计未发现端点、路径、凭据或业务标识。

Shell 语法、差异检查、拓扑合同直接执行及注册 CTest 1/1、完整构建和 OpenSpec 严格校验 24/24 通过。标准完整 CTest 共 1451 项：1444 项通过、7 项按环境门控跳过、0 失败，总耗时 526.15 秒；PgBouncer 定向门禁已由上述固定产物另行通过。本机固定源码构建不声明跨发行版二进制摘要一致，也不替代生产认证/TLS、稳定写端点 HA、独立故障域、连接/内存容量和版本升级回归；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.117 文件夹快照日期 helper 公开面收敛记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定文件夹快照日期转换的内部链接合同。全仓调用点、Git 历史和已编译对象审计确认，`DateToJson` 只在 `FileServiceUtils.cpp` 内被 `BuildFolderSnapshot` 调用 6 次，没有其他生产、测试、工具、客户端或迁移消费者。该 helper 在服务拆分前位于原 `FileService.cpp` 匿名命名空间，拆分后才被暴露到共享头文件，并在后端与测试二进制中各导出一个全局符号。

`DateToJson` 已移回 `FileServiceUtils.cpp` 匿名命名空间，共享头文件删除该声明和仅为它存在的直接 `trantor::Date` 依赖。新增源码/行为合同拒绝公开声明、外部链接定义和日期序列化回退；`BuildFolderSnapshot` 的 root/folders/files 结构及 created_at/updated_at 字段与 `toDbStringLocal()` 值保持不变，业务事务和回收站恢复语义未改动。

旧实现上新合同按预期为 0/1，精确检出 3 个公开面失败断言，6 个日期值断言仍通过。实现后直接 GoogleTest 3/3、相关回收站/文件夹/配额安全网聚焦 CTest 20/20、完整构建、拓扑合同、差异检查和 OpenSpec 24/24 通过；对象符号审计确认两个二进制均只保留匿名命名空间的局部符号。标准完整 CTest 共 1452 项：1445 项通过、7 项按环境门控跳过、0 失败，总耗时 546.38 秒。该批只收敛一个无外部消费者的公开符号；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.118 文件夹服务路径 helper 重复实现清理记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定 FolderService 路径构造的共享调用合同。全仓调用点、Git 历史、源码和已编译对象审计确认，`FolderService.cpp` 匿名命名空间的 `BuildFolderPath()`/`BuildFilePath()` 与 `FileServiceUtils.cpp` 共享实现逐字节相同，分别服务文件夹创建/重命名和后代文件路径更新共 3 个生产调用点。旧后端二进制因此同时保留两个 FolderService 局部副本与两个全局共享符号。

FolderService 已直接引入 `FileServiceUtils.hpp`，3 个调用点改用 `disk::file::utils::BuildFolderPath()`/`BuildFilePath()`，两个本地副本删除。根目录和嵌套目录下的文件/文件夹路径结果不变；名称校验、行锁/名称锁、事务、后代路径更新、父目录计数、错误和响应未改动。`ShareService.cpp` 的两个独立副本未在本批修改，留待后续单独审计。

旧实现上新源码合同按预期为 0/1，精确检出 5 个失败断言，同时共享 helper 路径行为 1/1 通过。实现后直接路径/后代更新合同 3/3、相关文件夹仓储/文件变更/生命周期/配额/路径安全网聚焦 CTest 20/20（31.59 秒）、完整构建、拓扑合同、差异检查和 OpenSpec 24/24 通过；对象符号审计确认 FolderService 两个局部符号消失。标准完整 CTest 共 1454 项：1447 项通过、7 项按环境门控跳过、0 失败，总耗时 549.01 秒。该批只清理 FolderService 的重复路径构造；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.119 分享保存路径 helper 重复实现清理记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定 ShareService 保存路径的共享调用合同。全仓调用点、Git 历史、源码和已编译对象审计确认，`ShareService.cpp` 匿名命名空间的 `BuildFolderPath()`/`BuildFilePath()` 早于 `FileServiceUtils` 共享 helper 出现，但当前两组实现逐字节相同。两个本地副本只服务 SaveToDrive 显式文件、副本根文件夹、后代文件夹和后代文件的 4 个路径赋值点；旧后端二进制因此同时保留两个 ShareService 局部副本与两个全局共享符号。

ShareService 已直接引入 `FileServiceUtils.hpp`，SaveToDrive 的 4 个调用点改用 `disk::file::utils::BuildFolderPath()`/`BuildFilePath()`，两个本地副本删除。共享 helper 已锁定的根目录和嵌套目录文件/文件夹路径结果不变；分享权限/状态/限流、目标归属、配额与内容引用结算、事务、项目计数、列表缓存失效、错误和响应未改动。

旧实现上新 ShareService 源码合同按预期为 0/1，精确检出 5 个失败断言，同时 FolderService 共享合同与共享路径行为 2/2 通过。实现后直接路径合同 3/3、分享/内容引用/列表缓存与六组真实分享流程聚焦 CTest 151/151（46.10 秒）、内容配额安全网 1/1（24.64 秒）、完整构建、拓扑合同、差异检查和 OpenSpec 24/24 通过；对象符号审计确认 FolderService/ShareService 四个局部符号均消失，仅保留两个共享符号。标准完整 CTest 共 1455 项：1448 项通过、7 项按环境门控跳过、0 失败，总耗时 529.15 秒。该批只清理 ShareService 的重复路径构造；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.120 分享创建 SQL 绑定模板重复实现清理记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定 ShareService 创建关联的共享 SQL 绑定合同。全仓调用点、Git 历史、源码和已编译对象审计确认，`ShareService.cpp` 匿名命名空间的 `ExecSqlWithBindings` 模板早于 `FileServiceUtils.hpp` 共享模板出现，但当前的 client/sql/binder 签名、binder 构造、调用方绑定和 `SqlAwaiter` 协程返回逐行相同。本地模板只被分享创建事务中 file/folder 两条批量 `share_files` 插入调用。

ShareService 的本地模板已删除，两个调用点改用 `disk::file::utils::ExecSqlWithBindings()`。共享模板的 binder 构造、调用方绑定和 awaiter 顺序以源码合同正向锁定；批次切分、SQL 占位符、file/folder 顺序与绑定值、事务归属/回滚、错误、审计和响应未改动。

旧实现上新合同按预期为 0/1，共享模板 4 个正向断言通过，精确检出本地模板/定义与两处未迁移调用对应的 3 个失败断言。实现后分享创建/原子性/密码/过期/所有权与真实管理/审计聚焦 CTest 19/19（8.96 秒）、完整构建、拓扑合同、差异检查和 OpenSpec 24/24 通过；对象符号审计确认 Share Create 的两个 lambda 实例均归属共享模板，不再存在 Share 匿名模板实例。标准完整 CTest 共 1456 项：1449 项通过、7 项按环境门控跳过、0 失败，总耗时 541.78 秒。该批只清理 ShareService 的重复 SQL 绑定模板；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.121 搜索单测全文 helper 重复实现清理记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定搜索单测直接覆盖生产全文 helper 的合同。全仓调用点、Git 历史和源码审计确认，`FileServiceSearch_test.cpp` 的匿名 `NormalizeFulltextKeyword()`/`IsFulltextEligible()` 与 `FileServiceUtils.cpp` 生产实现逐行相同；两组函数在同一个全文检索功能提交中同时加入，服务拆分只迁移了生产实现。测试目标已经链接 `FileServiceUtils.cpp`，本地副本仅被本文件 11 个 eligibility 断言调用。

搜索测试已引入 `FileServiceUtils.hpp`，删除两个本地副本和无用的 `<cctype>`/`<string_view>`，原 11 个接受/拒绝断言直接调用 `disk::file::utils::IsFulltextEligible()`。源码合同拒绝平行实现回归并锁定生产 helper 调用；关键词空格归一化、最小长度、ASCII 字母数字、非 ASCII/标点回退、全文/LIKE SQL 分支及生产搜索行为未改动。

旧实现上新合同按预期为 0/1，精确检出 4 个失败断言。实现后合同与搜索单测直接执行 7/7、搜索 DTO/SQL/helper 聚焦 CTest 22/22（0.34 秒）、真实文件元数据搜索集成 1/1（2.24 秒）、完整构建、分布式拓扑合同、差异检查和 OpenSpec 24/24 通过；测试二进制不存在测试匿名 helper 符号。标准完整 CTest 共 1457 项：1450 项通过、7 项按环境门控跳过、0 失败，总耗时 550.68 秒。该批只清理测试平行实现；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.122 文件列表排序列 helper 内部化记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定文件列表排序列解析的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，`disk::file::utils::ResolveListSortColumn()` 只有 `FileListQuery.cpp` 的 all/file/folder 三个生产调用点，没有其他消费者；该函数最初位于单体 `FileService.cpp` 匿名命名空间，服务拆分后才被共享头文件导出，并在后端与测试二进制各形成一个全局符号。

`ResolveListSortColumn()` 已移回 `FileListQuery.cpp` 匿名命名空间，三个调用点直接使用局部 helper，共享头文件和 `FileServiceUtils.cpp` 删除其声明与实现。size 在文件/混合列表映射为 `size`、在纯文件夹列表映射为 `sort_size`，两个时间字段原样映射，其他值回退 `name`；同时服务列表与搜索的 `BuildDeterministicOrderByClause()` 继续共享。DTO 白名单、升降序、稳定 tie-break、窄行分页、SQL、缓存键、错误和响应未改动。

旧实现上新合同按预期为 0/1，共享排序 builder 的 2 个正向断言通过，并精确检出 8 个失败断言。实现后列表合同/DTO 直接执行 9/9、列表合同/DTO/响应聚焦 CTest 14/14（0.24 秒）、真实文件元数据列表集成 1/1（2.28 秒）、完整构建、OpenSpec 24/24、分布式拓扑合同和差异检查通过；后端只保留一个局部排序列符号，测试二进制为零，共享排序 builder 保留。标准完整 CTest 共 1458 项：1451 项通过、7 项按环境门控跳过、0 失败，总耗时 531.31 秒。该批只收敛一个列表专用公开符号；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.123 健康时间戳 helper 内部化记录（2026-08-01）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定健康时间戳格式化的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `HealthService::GetTimestamp()` 自健康 API 初版起始终只有同一实现文件内的 `BuildBaseResult()` 一个调用方，没有测试、集成、工具、客户端、迁移或兼容消费者；旧后端与测试二进制各导出一个类成员符号。

`GetTimestamp()` 已原样移入 `HealthService.cpp` 匿名命名空间，头文件删除成员声明，`BuildBaseResult()` 继续在原位置调用。系统时钟、UTC 转换、Windows `gmtime_s`、Linux/macOS `gmtime_r` 与 `%Y-%m-%dT%H:%M:%SZ` 格式逻辑未改动；liveness/readiness 的 timestamp 和其他健康字段、角色依赖检查、错误脱敏与响应均保持不变。

旧实现上新合同按预期为 1/2：UTC 行为用例通过，源码边界用例精确检出 3 个失败断言。实现后直接健康 GoogleTest 8/8、健康/进程运行时/分布式拓扑/真实健康日志聚焦 CTest 19/19（4.16 秒）、完整构建、OpenSpec 24/24 和差异检查通过；两个制品均只保留匿名命名空间局部符号，不再导出旧类成员符号。标准完整 CTest 共 1460 项：1453 项通过、7 项按环境门控跳过、0 失败，总耗时 525.60 秒。该批只收敛健康格式化实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.124 系统构建时间 helper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定系统构建时间格式化的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `SystemService::GetBuildTime()` 自系统信息 API 初版起始终只有同一实现文件内的 `GetInfo()` 一个调用方，没有生产、测试、集成、工具、客户端、迁移或兼容消费者；旧后端导出一个类成员符号，测试二进制没有该符号。

`GetBuildTime()` 已原样移入 `SystemService.cpp` 匿名命名空间，头文件删除成员声明，`GetInfo()` 继续在原位置调用。`std::string(__DATE__) + " " + std::string(__TIME__)` 生成规则和公开 `build_time` JSON 字段未改动；版本、uptime、连接/存储统计、认证门禁、日志上下文、错误与响应均保持不变。系统日志源码合同不再用待内部化成员划定文本范围，改用外层 `disk::system` 命名空间结束标记，继续覆盖存储统计错误边界。

初次红测把结束标记改为 `GetConnectionStats()` 时，既有合同正确检出存储统计错误日志被截断；在实现前纠正锚点后，旧实现基线为 4/5，内部链接合同精确检出 3 个失败断言。实现后直接相关 GoogleTest 5/5、系统结构/SQL/日志/真实系统信息/分布式拓扑聚焦 CTest 17/17（4.36 秒）、完整构建、OpenSpec 24/24 和差异检查通过；后端只保留匿名命名空间局部符号，测试二进制仍无该符号。标准完整 CTest 共 1461 项：1454 项通过、7 项按环境门控跳过、0 失败，总耗时 528.55 秒。该批只收敛构建时间格式化实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.125 分享单消费者纯 helper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定分享单消费者纯 helper 的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `ShareService::GenerateShareCode()` 自分享功能初版起只有 `Create()` 一个调用方，`ShareService::GetStatusFilter()` 只有 `List()` 一个调用方；没有测试、集成、工具、客户端、迁移或兼容消费者，旧后端各导出一个类成员符号，测试二进制没有这两个符号。

两个函数体已原样移入 `ShareService.cpp` 既有匿名命名空间，头文件删除声明和仅描述类私有实现的注释，Create/List 继续各调用一次。分享码仍使用 8 位大小写字母数字随机串；all 与未知状态仍返回无过滤，active/expired/cancelled 仍映射现有 `ShareStatus`。日期、链接、状态有效性和密码等多调用 helper 继续留在类内；所有权校验、密码散列、事务插入、审计、分页 SQL、过期二次判定、权限、日志上下文、错误和公开响应均未改动。

旧实现上新合同按预期为 1/2，精确检出 6 个链接边界失败断言，全部规则与调用数量正向断言通过。实现后直接源码合同 2/2、分享列表 DTO/真实创建列表管理审计/分布式拓扑聚焦 CTest 14/14（13.86 秒）、完整构建、OpenSpec 24/24 和差异检查通过；后端只保留两个匿名命名空间局部符号，测试二进制仍无对应符号。标准完整 CTest 共 1462 项：1455 项通过、7 项按环境门控跳过、0 失败，总耗时 531.38 秒。该批只收敛两个单消费者实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.126 回收站恢复名称解析 helper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定回收站恢复名称解析的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `TrashService::ExtractBaseName()` 只有文件恢复冲突命名一个调用点，`TrashService::ExtractExtension()` 只有同一流程的原名解析和最终元数据写入两个调用点；没有外部消费者，旧后端与测试二进制各导出两个类成员符号。

两个函数体已原样移入 `TrashService.cpp` 匿名命名空间，头文件删除声明和仅描述类私有实现的注释，三个调用点保持原位。无点、首点、末尾点、普通后缀及点号前已有 ` (n)` 的专用规则未改动，也没有改用活跃文件写路径共享的 `ExtractFileExtension()`；候选命名、名称锁、事务、父计数、content 引用、配额、日志、错误和响应均保持不变。日志源码合同改用外层 `disk::trash` 命名空间结束标记，继续覆盖完整服务实现。

旧实现上 3 个定向合同按预期为 1/3，精确检出 7 个链接边界失败断言，规则与调用数量正向断言均通过。实现后 3 个直接合同、回收站查询顺序/真实生命周期/配额内容故障注入/分布式拓扑聚焦 CTest 12/12（27.98 秒）、完整构建、OpenSpec 24/24 和差异检查通过；两个制品均只保留两个匿名命名空间局部符号。标准完整 CTest 共 1463 项：1456 项通过、7 项按环境门控跳过、0 失败，总耗时 522.68 秒。该批只收敛恢复专用实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.127 健康响应 mapper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定健康响应映射的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `HealthController::ToResponse()` 自角色化探针引入起只有同一实现文件内的 `Live()` 与 `Ready()` 两个调用点；没有外部消费者，旧后端导出一个类成员符号，测试二进制没有该符号。

函数体已原样移入 `HealthController.cpp` 匿名命名空间，头文件删除成员声明，两个 handler 继续复用同一局部 mapper。`Response::Success(result.ToJson())` 统一信封与非 `healthy` 时设置 `drogon::k503ServiceUnavailable` 的规则未改动；三条公开路由、角色化探针、依赖检查、日志上下文、响应字段和状态语义均保持不变。

旧实现上 3 个定向用例按预期为 2/3，精确检出 4 个内部链接失败断言，调用数量、统一信封和 503 分支正向断言均通过。实现后 3 个直接用例、健康角色/Redis readiness/进程响应/真实健康日志/依赖故障注入/分布式拓扑聚焦 CTest 14/14（6.28 秒）、完整构建、OpenSpec 24/24 和差异检查通过；后端只保留一个匿名命名空间局部 mapper 符号，测试二进制仍无该符号。标准完整 CTest 共 1464 项：1457 项通过、7 项按环境门控跳过、0 失败，总耗时 542.34 秒。该批只收敛健康响应实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.128 认证用户响应 mapper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定认证用户响应映射的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `AuthService::UserToResponse()` 自认证服务初版起只有同一实现文件内注册与登录各一个调用点；没有外部消费者，旧后端导出一个类成员符号，测试二进制没有该符号。

函数体已原样移入 `AuthService.cpp` 匿名命名空间，头文件删除成员声明，Register/Login 继续各调用一次。id、username、email、nickname、storage_quota、storage_used、created_at 的 ORM 字段来源和空 nickname 回退 username 规则未改动；唯一性检查、密码散列、持久化、JWT/refresh token、登录计数、日志、错误和公开 JSON 均保持不变。

旧实现上 2 个定向合同按预期为 1/2，精确检出 4 个内部链接失败断言，调用数量、七个字段来源和昵称回退正向断言均通过。实现后 2 个直接合同、认证 DTO/日志/令牌/真实注册登录 refresh/分布式拓扑聚焦 CTest 10/10（12.15 秒）、完整构建、OpenSpec 24/24 和差异检查通过；后端只保留一个匿名命名空间局部 mapper 符号，测试二进制仍无该符号。标准完整 CTest 共 1465 项：1458 项通过、7 项按环境门控跳过、0 失败，总耗时 531.91 秒。该批只收敛认证响应实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.129 本地分片对象键 helper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定本地分片对象键生成的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `LocalFileStorage::GetChunkObjectKey()` 自不可变分片描述符引入起只有同一实现文件内 `WriteChunk()` 与 `ResolveChunkFilePath()` 两个调用点；没有外部消费者，旧后端与测试二进制各导出一个类成员符号。

函数体已原样移入 `LocalFileStorage.cpp` 既有匿名命名空间，头文件删除成员声明，两个调用点保持原位。不可变分片仍生成 `<upload_id>/chunks/<chunk_index>-<md5>.part` generic key，解析仍要求权威描述符完全相等，空 `object_key` 的旧 local 行仍回退 `<upload_id>/<chunk_index>.chunk`；输入校验、不可变写入与幂等重试、HEAD、组装、清理、inventory、日志和错误均未改变，也未移除 local staging 或任何迁移兼容路径。

旧实现上新增合同按预期为 0/1，精确检出 6 个内部链接与局部实现失败断言，两个调用点、描述符等值校验和 legacy 回退正向断言均通过。实现后直接合同 1/1、本地分片写入/HEAD/组装/不可变重试/篡改拒绝/legacy 行/清理/inventory/分布式拓扑聚焦 CTest 38/38（1.35 秒）、完整构建、OpenSpec 24/24 和差异检查通过；两个制品均只保留一个匿名命名空间局部 key-builder 符号。标准完整 CTest 共 1466 项：1459 项通过、7 项按环境门控跳过、0 失败，总耗时 550.59 秒。该批只收敛本地存储实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.130 Token verifier builder 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定 token verifier 构造的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `TokenService::BuildJwtVerifier()` 与 `BuildShareJwtVerifier()` 自认证 CPU pool 重构引入起只在同一实现文件内使用：普通 builder 由构造与 `Initialize()` 调用，分享 builder 另由显式临时密钥验证分支调用；没有外部消费者，旧后端与测试二进制各导出两个类成员符号。

两个函数体已原样移入 `TokenService.cpp` 既有匿名命名空间，并使用实现局部 `JwtTraits/JwtVerifier` 别名；头文件只删除两个 builder 声明，承载 `m_jwt_verifier` 与 `m_share_jwt_verifier` 的私有类型别名继续保留。两个 builder 仍使用 HS256 并分别要求 `disk`/`disk_share` issuer；单例构造、密钥初始化、已初始化 verifier 复用、显式临时分享密钥、claims、过期、撤销、CPU pool、日志和错误均未改变。

旧实现上新增合同按预期为 0/1，精确检出 11 个内部链接与局部实现失败断言，成员类型、调用数量、初始化赋值和临时密钥调用正向断言均通过。实现后直接合同 1/1，token 日志/share/access/refresh/撤销/JWT 与分享过滤器/真实认证生命周期/分享令牌安全/分布式拓扑聚焦 CTest 123/123（16.18 秒）、完整构建、OpenSpec 24/24 和差异检查通过；两个制品均只保留两个匿名命名空间局部 builder 符号。标准完整 CTest 共 1467 项：1460 项通过、7 项按环境门控跳过、0 失败，总耗时 529.44 秒。该批只收敛 token verifier 构造边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.131 分享无状态领域 helper 内部化记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定分享无状态领域 helper 的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有静态 `ShareService::IsShareExpired()`、`IsShareActive()`、`VerifyPassword()`、`FormatDateTime()` 与 `BuildShareLink()` 自分享服务初版起只在同一实现文件内使用；没有外部消费者，旧后端导出五个类成员符号，测试二进制没有对应符号。

五个函数体已原样移入 `ShareService.cpp` 既有匿名命名空间，头文件删除成员声明和仅描述类私有实现的注释。持久 `Active` 状态与过期时间继续共同决定有效性；无密码分享继续通过，有密码分享继续调用 `HashUtil::VerifyPassword()`；日期继续以本地时区 `%Y-%m-%d %H:%M:%S` 输出，链接继续为 `/s/<share_code>`。所有权、事务、计数、Redis 限流、审计、日志、错误和公开响应均未改动。

旧实现上新增合同按预期为 0/1，精确检出 23 个内部链接、局部实现与规则体失败断言，五组调用数量正向断言均通过。实现后直接合同 1/1，分享 DTO/服务契约/创建/访问/密码/管理/审计/令牌安全/分布式拓扑聚焦 CTest 142/142（32.95 秒）、完整构建、OpenSpec 24/24 和差异检查通过；后端只保留五个匿名命名空间局部符号，测试二进制仍无对应符号。标准完整 CTest 共 1468 项：1461 项通过、7 项按环境门控跳过、0 失败，总耗时 528.05 秒。该批只收敛分享无状态领域实现边界；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.132 分享日期格式并发安全记录（2026-08-02）

系统测试、单元测试和后端低风险清理 OpenSpec 先行固定分享日期格式的并发安全合同。全仓时间转换调用审计确认，分享响应的 `FormatDateTime()` 是后端唯一仍调用 `std::localtime()` 的路径；该入口返回进程级静态存储，多 Drogon 事件线程并发构建分享创建、列表、详情和更新响应时存在数据竞争风险。

`FormatDateTime()` 已改为直接返回 `trantor::Date::toCustomFormattedStringLocal("%Y-%m-%d %H:%M:%S", false)`，删除微秒换算、`std::localtime()` 静态缓冲区和手工 `std::put_time()`。提交前的 Trantor 1.5.24 头文件与对象码审计确认，`toDbStringLocal()` 会按值追加微秒，而最终选用的 custom local 入口在当前 Linux 制品中调用 `localtime_r` 且显式禁用微秒。公开日期字符串仍为本地时区 `%Y-%m-%d %H:%M:%S`，原 8 个调用点未改动；分享状态、权限、密码、链接、事务、计数、Redis 限流、审计、日志、错误和响应字段均未改动。

旧 `std::localtime()` 实现上初始合同按预期为 0/1，精确检出 4 个非重入调用、Trantor 直接返回和调用数失败断言，其他内部链接、调用数量和领域规则断言均通过。依赖审计发现中间 `toDbStringLocal()` 方案会按值追加微秒后，收紧的合同再次按预期为 0/1，精确检出禁用入口、最终 formatter 与调用数 3 个失败断言。

最终实现的直接源码/运行时 GoogleTest 2/2，分享 DTO/服务契约/创建/访问/密码/管理/审计/令牌安全/分布式拓扑聚焦 CTest 143/143（33.64 秒）、完整构建、Trantor 对象码 `localtime_r` 审计、后端 `std::localtime` 全局缺席审计、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1469 项：1462 项通过、7 项按环境门控跳过、0 失败，总耗时 529.06 秒。该批只修复分享日期格式的进程内并发安全；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.133 分享码密码学随机源记录（2026-08-02）

分享 OpenSpec、系统测试和单元测试文档先行固定分享码的密码学随机源合同。源码和调用点审计确认，`GenerateShareCode()` 每次以 `std::random_device` 的单次结果播种非密码学 `std::mt19937`；无密码分享则直接以该外部标识作为公开访问入口。

`GenerateShareCode()` 已改为使用进程启动阶段已初始化的 libsodium `randombytes_uniform()`，按完整字符表大小逐字符无偏采样；`<random>`、`std::random_device`、`std::mt19937` 和 `std::uniform_int_distribution` 已从生产实现删除。原 62 字符表、8 位长度、数据库唯一约束、Create 调用点、事务、链接、权限、密码、审计、日志、错误和公开响应均未改动。

旧实现上收紧的源码合同按预期为 0/1，精确检出 6 个旧依赖与缺失正向要求失败断言。实现后直接合同 1/1，分享创建 DTO/响应/原子性/所有权/真实浏览、密码、管理、审计、令牌安全及分布式拓扑聚焦 CTest 57/57（31.76 秒）、完整构建、OpenSpec 24/24 和差异检查通过，其中真实后端集成继续覆盖分享创建与后续访问。标准完整 CTest 共 1469 项：1462 项通过、7 项按环境门控跳过、0 失败，总耗时 535.92 秒。该批不扩展到数据库唯一冲突重试；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.134 分享码唯一冲突有界重试记录（2026-08-02）

分享 API、数据库设计、系统测试、单元测试和 OpenSpec 先行固定数据库裁决合同。审计确认 `uk_shares_code` 已是最终唯一约束，但 Create 过去通过 ORM 直接插入单个候选；任意候选碰撞都会把整个请求映射为内部错误，没有重新生成分享码。

Create 的分享行写入已改为显式 `INSERT ... ON CONFLICT (share_code) DO NOTHING RETURNING *`：同一事务最多尝试 5 个 libsodium 候选，空结果才继续生成，成功候选确定后才批量写 `share_files`；全部冲突时主动回滚并保留既有 `500/10006`，其他约束或数据库异常继续进入原事务错误路径。分享码格式、所有权、密码、期限、权限、链接、关联、提交后审计、日志脱敏和公开响应均未改变。

旧实现上收紧的源码合同按预期为 0/1，精确检出 7 个缺失/旧路径断言；真实 PostgreSQL 首候选冲突也按预期为 0/1，在 HTTP 透明成功断言处失败。实现后直接合同 1/1，新碰撞集成 1/1 并通过首碰撞恢复与连续 5 次碰撞耗尽共 16 条断言；分享创建 DTO/响应/原子性/所有权/真实浏览、密码、管理、碰撞、审计、令牌安全及分布式拓扑聚焦 CTest 58/58（34.63 秒）、完整构建、Python 语法、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1470 项：1463 项通过、7 项按环境门控跳过、0 失败，总耗时 550.67 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.135 请求追踪 UUID 密码学随机源记录（2026-08-02）

API、系统测试和可观测性 OpenSpec 先行固定服务端回退 UUID 的分布式唯一性基础合同。审计确认当前实现为每个线程以 `std::random_device` 单次结果播种 `std::mt19937`，其播种质量由标准库实现决定，无法为独立实例和事件线程提供项目可审计的系统熵源保证。

回退生成已收敛为启动阶段已初始化的 libsodium `randombytes_buf()`：一次填充 16 字节，显式设置 UUID v4 version/variant 位，再编码为原有 36 字节小写格式。`<random>`、`std::random_device`、`std::mt19937` 和逐半字节分布采样已删除；合法上游 ID、非法值回退、已有属性保护、响应和日志关联均保持不变。

旧实现上新增两项测试按预期为 1/2，源码合同以 7 个断言精确检出旧 PRNG 和缺失的密码学随机填充/位掩码；实现后直接测试 7/7，8 个线程经公开解析入口生成的 4096 个 UUID 全部符合 v4 格式且本轮无重复。进程准入/请求追踪/分布式拓扑/健康日志聚焦 CTest 18/18、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1472 项：1465 项通过、7 项按环境门控跳过、0 失败，总耗时 551.59 秒。该批只替换服务端请求 ID 的随机源；Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.136 登录锁定 PostgreSQL 一致性记录（2026-08-02）

认证 API、数据库设计、系统测试与身份 OpenSpec 先行固定跨实例账户保护合同。审计确认旧实现以 ORM 先读后写累计 `login_attempts`，并发 API 会丢增量；第 5 次失败还同时写 `status=2` 和本机时间计算的 `locked_until`，而登录/refresh 优先按 `status=2` 拒绝，导致 15 分钟截止永远不能自动恢复。锁定判断、成功时间和失败截止均依赖 API 进程时钟。

账户可用性、失败累计、15 分钟截止、成功重置和旧行规范化已收敛到 PostgreSQL `NOW()` 与条件 `UPDATE ... RETURNING`。自动锁定保持管理员状态 `status=1`，有效截止期间不再增加次数或延长截止；正确登录必须先原子清零失败状态再签发令牌。管理员 `status=2 + locked_until=NULL` 继续只由管理员解除，状态变更会清空自动失败字段；旧版 `status=2 + 已过期 locked_until` 只在正确认证成功门禁中规范化。数据库写失败不再被登录流程静默吞掉。

旧实现上的源码合同按预期为 0/1；双 API 真实用例也按预期为 0/1，并直接观察到 `status=2/login_attempts=4`，证明永久状态耦合与并发丢增量。实现后同一双 API 用例通过 12 个并发错误登录、精确 5 次锁定、截止稳定、两实例拒绝、refresh 拒绝、到期恢复、旧行规范化和管理员锁保持；认证源码/日志聚焦 22/22、相邻合同定向 3/3、认证集成 6/6、完整构建、Python 编译、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1473 项：1466 项通过、7 项按环境门控跳过、0 失败，总耗时 562.04 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.137 认证连接 peer IP 归一化记录（2026-08-02）

认证 API、数据库设计、系统测试和身份 OpenSpec 先行固定地址边界。审计确认 `AuthController` 向服务传入 `peerAddr().toIpPort()`；登录限流会在 Redis key builder 内去端口，但成功登录把原端点直接写入 `users.last_login_ip VARCHAR(45)`，logout 也把原端点直接写入 `operation_logs.ip_address VARCHAR(45)`。这会把每次连接的临时源端口当成审计数据，并使最长 39 字节 IPv6 加方括号、冒号和 5 位端口超过字段上限；登录状态写现已 fail closed，因此该输入会阻止令牌签发。

`AuthService` 的 Login/Logout 入口现使用既有 `RedisKeyPrefix::ExtractIPOnly()` 各归一化一次，并让限流、`last_login_ip`、登录成功清理和 logout 审计复用同一无端口值；内部 `UpdateLoginInfo()` 只接收已归一化地址。当前可信来源继续是 transport peer；仓库没有启用并配置 Drogon `RealIpResolver`，所以本批明确不采信可伪造的 `X-Real-IP`/`X-Forwarded-For`。

旧实现上的新增源码合同按预期为 0/1，精确检出登录/登出未统一归一化的 5 个断言；真实双 API 用例也按预期为 0/1，并直接读到 `last_login_ip='127.0.0.1:35952'`。实现后源码合同 1/1，最长 `[IPv6]:65535` 归一化为 39 字节纯 IPv6；同一真实用例通过并确认登录与登出持久字段都为 `127.0.0.1`，既有跨实例锁定、refresh、撤销、重启和 Redis 故障恢复流程同时保持通过。认证聚焦 CTest 26/26、完整构建、Python 编译、OpenSpec 24/24 和差异检查通过；标准完整 CTest 共 1474 项：1467 项通过、7 项按环境门控跳过、0 失败，总耗时 540.22 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.138 受信代理客户端 IP 边界记录（2026-08-02）

功能/API/数据库/部署/系统测试/单元测试与身份 OpenSpec 先行固定 L7 地址信任合同。审计确认 Nginx 已覆盖 `X-Real-IP` 并追加 `X-Forwarded-For`，但应用未启用解析器；分布式入口后的注册、登录和分享 IP 限流以及审计因此只看到代理 peer，使不同公网用户共享一个限流桶。直接在业务代码读取头部则会允许绕过限流和伪造审计来源。

Drogon 1.9.11 `RealIpResolver` 已在默认与分布式配置中启用，固定从单值 IPv4 `x-real-ip` 写 `disk-client-ip` attribute，默认 allowlist 为空；`DISK_TRUSTED_PROXY_CIDRS` 以 1 到 32 项的严格 JSON 非空字符串数组覆盖 `trust_ips`。Compose 为 Nginx 固定 `172.28.0.10` 并只信任该地址，Kubernetes 与环境模板要求部署者显式替换代理地址。依赖源码审计确认当前 Drogon 的受信 peer CIDR 与头部客户端地址解析都只支持 IPv4；代理后的 IPv6 客户端会回退到代理 peer，参考入口不得宣称已区分该流量族。

统一 `ResolveClientIp()` 只消费解析器 attribute，缺失时回退 transport peer，绝不自行读取 `X-Real-IP` 或 `X-Forwarded-For`。注册、登录和公开分享 IP 限流，以及认证、分享、存储恢复和存储任务管理审计全部复用该边界。旧实现上的客户端 IP/运行时配置合同按预期为 0/3，真实双 API 用例也按预期为 0/1，并观察到伪装客户端登录仍持久化 `127.0.0.1`。

实现后客户端 IP 与运行时配置直接测试 14/14、相邻限流和审计聚焦测试 70/70、分布式拓扑合同和双 API 认证一致性均通过；真实数据库分别保存登录 `198.51.100.27` 与 logout `203.0.113.41`。完整构建、Python 编译、差异检查和 OpenSpec 24/24 通过；标准完整 CTest 共 1479 项：1472 项通过、7 项按环境门控跳过、0 失败，总耗时 536.45 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.139 受信代理 CIDR 启动前语义校验记录（2026-08-02）

运行时配置、部署、系统测试、单元测试与 OpenSpec 先行固定 allowlist 语义。审计确认当前 `ParseStringArray()` 只检查 JSON 数组和非空字符串；`not-an-ip`、IPv6、端口、`/0`、越界前缀和带主机位 CIDR 会进入 Drogon 插件初始化，依赖异常可能回显原始配置，`10.20.0.1/24` 还会被静默扩大为整个 `10.20.0.0/24`。

唯一 `RuntimeConfig::LoadFromEnvironment()` 管线现会解析严格点分十进制 IPv4 与规范 CIDR，要求前缀 `1..32` 且 CIDR 主机位全零，并在写入 `RealIpResolver.config.trust_ips` 前拒绝 IPv6、端口、空白、前导零和所有畸形/过宽输入。错误只包含环境变量名；合法精确地址与规范网络保持原字符串和顺序。

旧实现上的语义反例直接测试按预期为 0/1，在首个 `not-an-ip` 输入处证明旧管线错误放行。实现后客户端 IP 与运行时配置直接测试 14/14、分布式拓扑合同、完整构建、OpenSpec 24/24 和差异检查均通过；标准完整 CTest 共 1479 项：1472 项通过、7 项按环境门控跳过、0 失败，总耗时 552.92 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.140 受信代理最终配置启动门禁记录（2026-08-02）

运行时配置、部署、系统测试、单元测试与 OpenSpec 先行固定最终配置合同。审计确认 15.139 只校验 `DISK_TRUSTED_PROXY_CIDRS` 覆盖值；未设置该变量时，自定义 `DISK_CONFIG_FILE` 仍可携带非法 `trust_ips`，也可删除或复制 `RealIpResolver`、把 `from_header` 改为可追加的 `x-forwarded-for`，或把 `attribute_key` 改到业务 helper 不读取的位置。环境覆盖存在时才调用的 `PluginConfig()` 不能保护这些启动路径。

环境覆盖完成后现会统一验证最终生效配置：必须恰有一个 `RealIpResolver`，固定 `from_header=x-real-ip` 与 `attribute_key=disk-client-ip`，并带 0-32 项已通过 15.139 同一语义规则的 `trust_ips` 数组。缺失、重复、畸形、重定向和非法 allowlist 都会在 Drogon 插件初始化前以不回显配置值的固定诊断失败；环境变量覆盖仍保持非空 1-32 项要求。共享滚动升级集成夹具同步加入空 allowlist resolver 与 GlobalFilters 显式依赖，使旧/新制品使用同一配置边界。

旧实现上的 10 组自定义 JSON 反例直接测试按预期为 0/1，全部被错误放行；实现后客户端 IP 与运行时配置直接测试 15/15，分布式拓扑、双 API 认证一致性、首轮受影响集成复验 13/13、完整构建、Python 编译、OpenSpec 24/24 和差异检查通过。首轮完整套件精确暴露 13 个共享夹具缺失 resolver 的启动失败，修正唯一 `server_config()` 后全部复验通过；第二轮仅既有内容/配额安全网 397 条断言中的删除后内容行收敛偶发失败，定向复跑 1/1 通过且未放宽断言。第三轮标准完整 CTest 共 1480 项：1473 项通过、7 项按环境门控跳过、0 失败，总耗时 540.97 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.141 受信客户端 IP 插件拓扑启动门禁记录（2026-08-02）

运行时配置、部署运维、系统测试、单元测试与 OpenSpec 先行固定插件拓扑合同。Drogon 1.9.11 源码审计确认，插件管理器仅对配置中显式声明的 `dependencies` 做深度优先初始化；缺少该边时，初始化次序取决于插件列表顺序。15.140 只校验最终 `RealIpResolver` 配置，自定义 `DISK_CONFIG_FILE` 仍可删除或复制 `GlobalFilters`，把 `dependencies` 改成非数组，或删除/重复 resolver 依赖，而在应用启动管线中不被拒绝。

最终生效配置现必须恰有一个 `GlobalFilters`，其 `dependencies` 必须是字符串数组并且恰好一次包含 `drogon::plugin::RealIpResolver`。缺失、重复、畸形依赖列表和 resolver 依赖缺失/重复都在 Drogon 插件初始化前以不回显配置值的固定诊断失败。原 `PluginConfig()` 查找逻辑已抽为内部必需插件 helper，环境覆盖仍只取 resolver 的 `config`；新拓扑校验只读取 `GlobalFilters.dependencies`，不改写过滤器列表、不新增插件也不改变请求路由与认证语义。

旧实现上的聚焦反例测试按预期为 0/1，同一用例的 7 组断言全部证明无效拓扑被错误放行。实现后运行时配置直接测试 12/12，客户端 IP、过滤器所有权、滚动升级、分布式拓扑和双 API 认证聚焦 CTest 30/30，完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1481 项：1474 项通过、7 项按环境门控跳过、0 失败，总耗时 535.67 秒。Phase 6/9/10 与最终 Definition of Done 继续保持未勾选。

### 15.142 文件夹树构造 helper 内部化记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定文件夹树构造的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有 `FolderService::BuildTreeFromFlatList()` 自 2026-02-14 文件夹树 API 初版起始终只有同一实现文件中 `GetFolderTree()` 的一个调用方，没有测试、集成、工具、客户端、迁移或兼容消费者。函数不读取任何服务成员；当前生产对象导出一个全局成员符号，四个 coroutine 代码路径重定位仍全部来自同一对象。

`BuildTreeFromFlatList()` 的函数体已原样移入 `FolderService.cpp` 匿名命名空间，头文件删除成员声明和无用 `<vector>` 依赖，实现文件显式所有 `<functional>`。扁平节点按 `parent_id` 分组、递归构造、移动语义、根 ID/名称、子节点顺序和唯一 `GetFolderTree()` 调用保持不变。重建后对象只保留匿名命名空本地 `t` helper，后端二进制不再包含旧成员符号。

旧实现上的源码合同按预期为 0/1，精确检出头文件声明和成员定义两个失败点。实现后源码/DTO 直接测试 10/10、文件夹命名聚焦 CTest 105/105、完整构建、OpenSpec 24/24 和差异检查通过。首轮完整套件唯一失败是未改动的分享过期令牌过滤器用例在断言前偶发超时；Redis 同期 `PONG`、无 blocked client，定向复跑 1/1（0.02 秒）通过。第二轮标准完整 CTest 共 1482 项：1475 项通过、7 项按环境门控跳过、0 失败，总耗时 542.17 秒。本批不删除迁移字段、兼容分支、本地暂存或部署能力，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.143 健康依赖检查 runner 内部化记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定健康依赖检查 runner 的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有 `HealthService::RunComponentCheck()` 自 2026-07-20 角色化健康探针引入起始终只有同一实现文件中 readiness 的 database、staging storage、final storage、Redis 与 storage job queue 五个调用方，没有测试、集成、工具、客户端、迁移或兼容消费者。函数只读取四个参数，不依赖 `HealthService` 实例状态；当前生产对象导出一个全局成员符号，其 coroutine 重定位全部来自同一对象。

函数体已原样移入 `HealthService.cpp` 既有匿名命名空间并从头文件删除成员声明；依赖 runtime state 与 start time 的 `BuildBaseResult()` 继续作为成员保留。角色化 callback 选择、缺失 callback 与异常处理、warning 日志上下文、调用方固定失败消息净化、健康消息清除、latency、总状态聚合、公开健康字段和响应均未改变。重建后的生产对象只保留匿名命名空间本地 `t` runner，后端不再包含旧成员符号。

旧实现上的源码合同按预期为 0/1，精确检出头文件声明、类限定定义和缺失局部定义三个失败点，调用数量断言通过。实现后直接健康测试 10/10、健康与真实日志聚焦 CTest 11/11、完整构建、OpenSpec 24/24、符号审计和差异检查通过。标准完整 CTest 共 1484 项：1477 项通过、7 项按环境门控跳过、0 失败，总耗时 560.89 秒。该批不改变部署拓扑、持久化 schema、缓存、认证、存储或任务状态机，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.144 DTO 首尾空白裁剪共享记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定 DTO 空白裁剪共享合同。全仓源码和 Git 历史审计确认，`StorageJobReplayRequest`、存储恢复命令 `Parser` 和 `UpdateProfileRequest` 分别维护一份等价的首尾 `std::isspace` 裁剪实现；三者均已继承 `DtoBase`，生产调用分别为一个重放原因、一个共享恢复原因解析点和 nickname/avatar 两个资料字段。三份实现都把字符转换为 `unsigned char` 后分类，对空串、全空白、空格/制表/换行及非 ASCII 内容的结果等价。

规则已集中为 protected `DtoBase::TrimWhitespace()`，三个 DTO 改用共享 helper 并删除本地实现与无用 include。源码合同锁定唯一共享定义、四个生产调用和零本地定义；重放/恢复原因的必填与 256 字节上限、用户资料的空值忽略与字段长度、JSON、错误码/消息、日志、确认门禁、审计及服务行为均未改变。共享实现继续以 `unsigned char` 调用 `std::isspace`，避免高位字节触发未定义行为。

旧实现上的源码合同按预期为 0/1，12 个断言完整检出共享定义缺失、三个本地定义、四个未迁移调用和三处专属 `<cctype>` 依赖。实现后共享基类与三个 DTO 直接测试 26/26、真实存储管理和用户资料集成 2/2、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1485 项：1478 项通过、7 项按环境门控跳过、0 失败，总耗时 542.55 秒。该批不改变持久化 schema、事务、缓存、认证、存储或分布式部署拓扑，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.145 系统连接统计 collector 内部化记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定系统连接统计 collector 的内部链接合同。全仓调用点、Git 历史、源码和已编译对象审计确认，私有 `SystemService::GetConnectionStats()` 自系统信息 API 初版起始终只有同一实现文件中 `GetInfo()` 的一个调用方，没有测试、集成、工具、客户端、迁移或兼容消费者。函数只读取全局 `ConfigMgr`，不依赖 `m_db_client`、`m_start_time` 或其他服务实例状态；当前生产对象导出一个全局成员符号及其 coroutine 本地符号，测试二进制没有对应符号。

`GetConnectionStats()` 已原样移入 `SystemService.cpp` 既有匿名命名空间并从头文件删除成员声明；依赖实例数据库客户端的 `GetStorageStats()` 继续作为成员保留。`db_pool_size`、`redis_pool_size` 仍取自最终运行配置，Drogon 不暴露活跃连接数时 `current`/`peak` 仍以 DB pool size 作为上限估算；公开 connections JSON、构建时间、uptime、存储统计、认证、日志上下文、错误和响应均未改变。

旧实现上的源码合同按预期为 0/1，精确检出头文件声明、类限定定义和缺失局部定义三个失败点，配置 getter、四个字段赋值与调用数量正向断言均通过。实现后直接系统合同/结构测试 16/16、真实系统信息与分布式拓扑聚焦 CTest 19/19（4.21 秒）、完整构建、OpenSpec 24/24、符号审计和差异检查通过；生产对象只保留匿名命名空间局部 collector 及其 coroutine 符号，测试二进制仍无对应符号。标准完整 CTest 共 1486 项：1479 项通过、7 项按环境门控跳过、0 失败，总耗时 561.74 秒。该批不改变配置值、持久化 schema、事务、认证、存储或部署拓扑，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.146 文件夹 DTO ASCII 空格裁剪共享记录（2026-08-02）

API、系统测试、单元测试与后端低风险清理 OpenSpec 先行固定文件夹名称裁剪合同。全仓源码与 Git 历史审计确认，`CreateFolderRequest` 和 `RenameFolderRequest` 分别维护一份逐行相同的私有 `TrimName()`，都用 `find_first_not_of(' ')`/`find_last_not_of(' ')` 只裁剪首尾 ASCII 空格；两个调用点没有其他生产、测试、客户端、迁移或兼容消费者，当前测试二进制分别生成一个弱成员符号。

两处调用应改用 `FolderDto.hpp` 内唯一的 `folder_dto_detail::TrimAsciiSpaces()`，删除两个私有变更方法和无用 `<algorithm>` 依赖。共享 helper 必须继续只识别 U+0020：普通首尾空格和全空格名称结果不变，制表符、换行及其他控制字符不得像 `DtoBase::TrimWhitespace()` 一样被静默裁掉，而应继续进入禁止字符校验；长度、保留/隐藏名称、UTF-8、日志、错误码/消息和公开请求行为均不得改变。

旧实现上的源码合同按预期为 0/1，精确检出一个共享实现、两个限定调用、删除两个 `TrimName()` 和移除无用 `<algorithm>` 的 6 个失败断言；新增 DTO 行为基线 38/38 通过。实现后直接源码/DTO 测试 43/43、文件夹与名称验证聚焦 CTest 113/113（6.02 秒）、完整构建、OpenSpec 24/24、符号审计和差异检查通过；后端与测试二进制各只保留一个共享 helper 弱符号，旧成员符号均消失。标准完整 CTest 共 1490 项：1483 项通过、7 项按环境门控跳过、0 失败，总耗时 550.09 秒。该批不改变名称合同、验证顺序、公开错误或 API 行为，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.147 可空数据库行读取共享记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定可空数据库字段读取合同。全仓源码与 Git 历史审计确认，`UploadDiagnosticService.cpp` 和 `StorageRecoveryAdminService.cpp` 分别维护逐行相同的通用 `OptionalValue<Row, T>()`，`StorageJobAdminService.cpp` 另有语义相同的字符串特化 `OptionalString<Row>()`；三者自各自管理功能初版起独立存在，共服务 16 个生产调用点，没有客户端、迁移或兼容消费者。

三份局部实现应收敛到 `utils/DbRowUtils.hpp` 内唯一的 `disk::utils::OptionalRowValue<T>()` 模板。所有调用必须继续先检查 `row[field].isNull()`：SQL NULL 返回 `std::nullopt`，非 NULL 才调用原有 `as<T>()`，字段名、目标类型、转换异常、查询、事务、日志、审计、错误和公开响应均不得改变；不应把管理领域的行映射器或 SQL 投影一并抽象。

旧实现上的新增源码合同按预期为 0/1，精确检出共享头/实现、三个 include、16 个限定调用和删除三个局部 helper 的 13 个失败断言；不依赖新结构的管理 DTO 与日志合同基线 16/16 通过。实现后直接源码/管理 DTO 测试 17/17、存储任务/恢复/诊断与分布式拓扑聚焦 CTest 56/56（13.17 秒）、完整构建、OpenSpec 24/24、符号审计和差异检查通过；后端只生成共享模板的字符串、`int32_t` 和 `uint64_t` 实例化，测试二进制只生成其实际使用的字符串实例化，旧局部 helper 符号均消失。标准完整 CTest 共 1491 项：1484 项通过、7 项按环境门控跳过、0 失败，总耗时 542.66 秒。该批不改变 SQL、字段、事务、日志、审计、错误或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.148 JSON 请求关联字段写入共享记录（2026-08-02）

系统测试、单元测试与后端低风险清理 OpenSpec 先行固定 JSON 请求关联合同。全仓源码与 Git 历史审计确认，管理员操作审计、分享审计、存储任务管理、存储恢复管理和下载完整性对账分别维护语义相同的局部 helper：都把非空 `request_id`/`operation` 原样写入 details，缺失或空字符串写 JSON null；五份定义共服务 12 个生产调用点，没有客户端、迁移或兼容消费者。

五个服务应改用 `LogHelper` 唯一的 `disk::utils::SetRequestCorrelationFields()` 并删除四个 `SetLogContext()` 与一个 `AddCorrelationDetails()`。共享函数必须复用日志器既有的可空字符串规则，只写 `request_id` 和 `operation`，保留其他 details 字段，且不得加入 `upload_id`、`job_id`、`lease_owner` 或 `state_version`；领域事件构造、finding/audit 内容、SQL、事务、日志、错误和公开响应均不得改变。JSON 序列化与字符串截断具有不同消费者和职责，本批不一并抽象。

旧实现上的新增源码合同按预期为 0/1，精确检出共享声明/定义、两次可空写入、管理员显式 include、12 个限定调用和删除五个局部 helper 的 15 个失败断言；日志、分享审计、管理边界和下载响应行为基线 29/29 通过。实现后直接源码/行为测试 31/31、日志/审计/下载/存储管理与分布式拓扑聚焦 CTest 36/36（17.56 秒）、完整构建、OpenSpec 24/24、符号审计和差异检查通过；后端与测试二进制各只保留一个共享函数符号，旧局部符号均消失。标准完整 CTest 共 1493 项：1486 项通过、7 项按环境门控跳过、0 失败，总耗时 555.78 秒。该批不改变 details 业务字段、持久化、日志、错误或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.149 Redis 领域错误脱敏记录（2026-08-03）

API、部署、系统测试、单元测试与 OpenSpec 先行固定 Redis 领域错误边界。`RedisService` 的命令异常和结果解析异常必须只返回 `RedisOperationFailed` 默认消息，缺失 key 只返回 `RedisKeyNotFound` 默认消息；hiredis/代理异常正文、endpoint、连接细节、实际 key、value、命令参数和凭据不得进入 `ErrorInfo` 或 HTTP JSON。

实现与测试必须保留既有错误码、HTTP 状态、Redis 命令、TTL/Lua/CAS、结构化日志的固定命令名和调用方关联、依赖指标分类，以及认证 fail-closed、限流 fail-open、文件列表缓存降级和 readiness 行为。源码合同应拒绝 `ex.what()` 或 key 拼入 `ErrorInfo`，真实 wrong-type Redis 行为应验证错误消息固定且日志继续脱敏。

旧实现上的三个定向合同按预期全部失败，精确检出缺失 key 回显、9 条依赖/解析异常正文拼接和 wrong-type 领域消息不稳定。实现后定向红绿测试 3/3、Redis/认证撤销/限流/健康/故障切换聚焦 CTest 167/167（63.15 秒）、完整构建、OpenSpec 24/24 和源码审计通过；9 条 `RedisOperationFailed` 与 1 条 `RedisKeyNotFound` 均只使用默认构造，生产实现不再包含 `ex.what()` 或带 key 的缺失消息。标准完整 CTest 共 1493 项：1486 项通过、7 项按环境门控跳过、0 失败，总耗时 537.53 秒。该批不改变 Redis 协议、错误码、HTTP 状态、指标或上层故障策略，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.150 S3 provider 领域错误脱敏记录（2026-08-03）

API、分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定 S3 错误边界。`S3Client` 的 13 条 SDK 失败映射必须只返回固定 `S3 <Operation> failed`，批量删除部分失败只返回固定 `S3 DeleteObjects partially failed`；provider code/message、endpoint、bucket、对象 key、multipart ID、签名和凭据不得进入 `ErrorInfo`、HTTP JSON 或 Worker 持久错误。

实现与测试必须保留现有业务错误码、HTTP 状态、SDK 调用、重试分类与预算、operation/outcome 日志、依赖指标、multipart abort、Worker 重试/死信和 readiness 行为。源码合同应证明 `GetMessage()` 不再参与领域错误构造，`GetCode()` 只保留批量错误的低基数分类用途，并锁定 13 条固定 SDK 操作消息与一个固定部分失败摘要。

旧实现上的新增源码合同按预期为 0/1，精确检出 helper 旧签名、3 处 `GetMessage()`、2 处 `GetCode()`、动态 provider 文本拼接和缺失固定返回共 6 个失败断言。实现后直接源码合同 1/1、S3 客户端/对象存储/工厂/Worker/下载响应/分布式拓扑与故障注入聚焦 CTest 80/80（4.56 秒）、完整构建、OpenSpec 24/24 和差异检查通过；源码审计确认 13 条调用统一经过固定 mapper，生产实现中 `GetMessage()` 为 0，`GetCode()` 只剩 1 处低基数结果分类，批量部分失败只返回固定摘要。标准完整 CTest 共 1494 项：1487 项通过、7 项按环境门控跳过、0 失败，总耗时 554.42 秒。该批不改变 S3 协议、错误码、HTTP 状态、重试、指标或 Worker 状态机，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.151 Worker 持久错误脱敏记录（2026-08-03）

API、数据库、分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定持久任务错误边界。新 Worker 写入 `storage_jobs.last_error` 时，只允许任务合同解析/构建产生的固定校验文本、固定配置/状态摘要或固定的 `staging_cleanup`、`multipart_abort`、`blob_gc`、`expire_uploads`、`expire_trash`、`storage_reconcile` 操作失败摘要；未知任务类型不得回显实际类型。下游 `ErrorInfo.message`、数据库/标准异常正文、SQL、连接信息、endpoint、对象定位符、payload 值和凭据不得进入新持久错误。

同一 Worker 的事务回滚、认领预检、心跳、handler、结果回写和批次认领异常日志也必须使用固定事件摘要，不记录 `what()`；既有类型化 request/upload/job/lease/version 关联、低基数 operation、错误码驱动的临时/永久分类、退避预算、owner 条件回写、死信、管理员字段、payload 校验和 readiness 行为保持不变。本批不改写历史 `last_error` 行，也不执行 schema/data migration。

旧实现上的四个定向测试按预期全部失败，共 10 个失败断言：五个行为断言直接检出连接串、bucket/key、provider 文本和未知任务类型回显，源码合同检出 12 次 `error.message` 文本出现、1 次错误码拼接、8 次 `what()` 和缺失固定 handler 摘要。实现后定向测试 4/4、Worker 仓储/合同/运行时/S3/Blob GC/multipart/接管/任务运维聚焦 CTest 106/106（78.06 秒）、完整构建、OpenSpec 24/24 和差异检查通过；生产实现中 `error.message`、`error.CodeInt()` 和 `.what()` 均为 0，六类依赖操作、未知类型、Blob GC 异常及顶层 handler 都只生成固定摘要。标准完整 CTest 共 1495 项：1488 项通过、7 项按环境门控跳过、0 失败，总耗时 534.47 秒。该批不改变 SQL、schema、重试分类/预算、租约、死信或管理员字段，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.152 TokenService JWT 异常脱敏记录（2026-08-03）

ADR、部署、系统测试、单元测试与 OpenSpec 先行固定认证异常边界。access、refresh 和 share token 的验签、解析、撤销 JTI 提取及分享令牌生成失败只允许固定完整日志消息，不得拼接 `what()`、JWT/provider 异常正文、claim、token、secret、endpoint 或连接信息；既有调用方 request/instance/operation、日志级别和所有权字段保持不变。

过期分类必须比较 `jwt-cpp` 的 `token_verification_exception::code()` 与 `token_verification_error::token_expired`，不得搜索英文 `expired`。三类过期 token 继续返回既有 `TokenExpired`，其他验签/解析错误映射、签名、claim、TTL、Redis key/CAS/撤销、CPU pool、指标和公开响应不变。

旧实现上的 7 个定向测试按预期为 3 通过、4 失败，共 14 个失败断言：源码合同检出缺失结构化 helper、12 次 `.what()`、3 处英文过期判断和 4 条动态失败日志，行为断言直接检出 5 条异常正文；三类过期行为仍通过。实现后定向测试 7/7、Token/认证过滤器/刷新与分享安全/Redis 故障切换/分布式拓扑聚焦 CTest 145/145（51.35 秒）、完整构建、OpenSpec 24/24 和差异检查通过。首次完整回归发现安全网集成仍匹配旧带冒号日志标记，同步为固定消息后独立复验 1/1（121.68 秒）；最终标准完整 CTest 共 1495 项：1488 项通过、7 项按环境门控跳过、0 失败，总耗时 540.87 秒。生产实现中 `.what()` 和英文 `expired` 搜索均为 0，三类验签统一使用结构化错误码 helper。该批不改变签名、claim、TTL、Redis、错误码或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.153 AuthService 依赖错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定认证业务层的依赖错误边界。唯一性检查、注册写入、refresh 用户查询/处理、logout 审计、用户查找、账户状态校验、登录状态更新和登录失败计数异常，以及登录限流检查/计数清理下游失败，只允许 11 条固定完整日志消息；PostgreSQL/标准异常正文、SQL、连接信息、Redis `ErrorInfo.message`、key/value、凭据和 token 不得进入这些事件。

现有业务成功与拒绝诊断、调用方 request/instance/`auth` 关联及所有权空值保持不变。用户名/邮箱冲突、密码校验、数据库时间锁定、限流 fail-open、refresh CAS、logout 撤销与审计 fail-open、错误码、HTTP 状态和公开响应不变。

旧实现上的定向源码合同按预期为 0/1，共 13 个失败断言：精确检出 9 处 `.what()`、2 处 Redis `.error().message` 转发及缺失的 11 条固定完整摘要。实现后定向测试 1/1、认证/登录限流/Redis/refresh/share/双实例/上传安全网聚焦 CTest 170/170（163.55 秒）、完整构建、OpenSpec 24/24 和差异检查通过。首次完整回归唯一失败是无关的内容 GC 收敛检查；独立复验先被上一轮异常退出遗留的 60 秒上传限流窗口阻挡，键自然过期后内容/配额安全网 1/1 通过（24.52 秒），随后标准完整 CTest 共 1495 项：1488 项通过、7 项按环境门控跳过、0 失败，总耗时 545.90 秒。`AuthService.cpp` 中 `.what()` 和 `.error().message` 均为 0，11 条依赖失败路径只记录固定摘要。该批不改变 SQL、数据库时间锁定、限流降级、JWT/Redis、审计、错误码或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.154 UserService 结构化未找到与依赖错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定用户资料业务层的依赖错误边界。资料读取数据库/处理异常、密码修改数据库/处理异常、资料更新数据库/处理异常和存储统计数据库异常只允许 7 条固定完整日志消息；PostgreSQL/标准异常正文、SQL、连接信息、凭据和 token 不得进入这些事件。

密码修改和资料更新必须分别捕获 `drogon::orm::UnexpectedRows` 并返回既有 `UserNotFound`，不得搜索 `condition`、`empty` 或其他英文异常文本。资料查询、字段更新、密码验证/哈希、聚合统计、其他错误码、HTTP 状态和公开响应保持不变。

旧实现上的定向源码合同按预期为 0/1，共 13 个失败断言：精确检出 9 处 `.what()`、各 2 处 `condition`/`empty` 英文搜索、零个结构化 `UnexpectedRows` 捕获及缺失的 7 条固定完整摘要。实现后定向测试 1/1、UserService/用户资料与存储/PostgreSQL 故障切换/上传安全网聚焦 CTest 13/13（143.12 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1495 项：1488 项通过、7 项按环境门控跳过、0 失败，总耗时 555.90 秒。`UserService.cpp` 中 `.what()` 和两类英文搜索均为 0，两处 `UnexpectedRows` 在通用数据库异常前结构化处理。该批不改变 SQL、资料字段、密码验证/哈希、聚合统计、其他错误码或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.155 SystemService 存储统计错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定系统信息聚合统计的依赖错误边界。数据库失败事件只允许固定完整消息 `Failed to get storage stats`；PostgreSQL 异常正文、SQL、连接信息、凭据和 token 不得进入该事件。

数据库失败后继续返回已取得或默认的存储统计，系统信息认证、版本、运行时间、连接统计、查询 SQL、request/instance/`system_info` 关联和公开成功响应保持不变。

旧实现上的定向源码合同按预期为 0/1，共 2 个失败断言：精确检出 1 处 `.what()` 和缺失的固定完整摘要。实现后定向测试 1/1、SystemService/真实系统信息/PostgreSQL 故障切换/上传安全网聚焦 CTest 10/10（135.88 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1495 项：1488 项通过、7 项按环境门控跳过、0 失败，总耗时 541.29 秒。`SystemService.cpp` 中 `.what()` 为 0，数据库异常捕获后仍直接返回现有 `StorageStats`。该批不改变认证、查询、降级、日志上下文或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.156 CleanupService 依赖错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行固定手动过期上传清理的依赖错误边界。数据库异常和标准处理异常只允许固定完整消息 `Database error cleaning expired upload tasks` 与 `Unexpected error cleaning expired upload tasks`；异常正文、SQL、连接信息、对象定位符、凭据和 token 不得进入这些事件。

两类失败继续返回既有 `InternalError` 与 `Failed to clean expired upload tasks`。回收站优先的组合顺序、100 项上传批次上限、Lifecycle 调用、计数、request/instance/`cleanup` 关联和公开响应保持不变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：精确检出 2 处 `.what()` 及缺失的两条固定完整摘要；两条既有公开错误消息断言通过。实现后定向测试 1/1、CleanupService/领域提取/上传与内容配额安全网聚焦 CTest 17/17（149.80 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1496 项：1489 项通过、7 项按环境门控跳过、0 失败，总耗时 546.08 秒。`CleanupService.cpp` 中 `.what()` 为 0，两条失败路径继续返回相同 `InternalError` 与公开消息。该批不改变组合顺序、批次、Lifecycle、计数、上下文或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.157 ShareAuditService fail-open 错误脱敏记录（2026-08-03）

API、分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一分享审计失败合同。数据库与标准异常路径只允许固定完整消息 `Failed to record share audit event` 与调用方既有类型化上下文；action、内部分享 ID、分享码、异常正文、SQL、连接信息、凭据和 token 不得进入失败日志。

审计继续 fail-open 且不自动重试。成功审计行的 action、target、details、IP 与 User-Agent 截断，以及创建、访问、口令失败、下载、逐项取消的业务结果和公开响应保持不变。

旧实现上的定向源码合同按预期为 0/1，共 2 个失败断言：精确检出 2 处 `.what()`，且固定完整语句计数为 0、预期为 2。实现后定向测试 1/1、分享审计/下载响应/真实分享与下载流程/分布式拓扑聚焦 CTest 25/25（34.16 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1496 项：1489 项通过、7 项按环境门控跳过、0 失败，总耗时 536.83 秒。`ShareAuditService.cpp` 中 `.what()`、`action=` 与 `share_code=` 均为 0，固定失败消息精确出现 2 次。该批不改变成功审计内容、fail-open/无重试、日志上下文、业务结果或公开响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.158 UploadService 依赖错误脱敏记录（2026-08-03）

API、分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一上传服务依赖错误合同。分片元数据记录数据库异常与 staging 会话读取标准异常只允许固定完整消息 `Failed to record chunk upload` 与 `Failed to load upload staging session`；upload/user ID、异常正文、SQL、连接信息、对象定位符、凭据和 token 不得进入失败消息，调用方既有类型化上下文保持不变。

两条路径继续返回既有 `InternalError` 与同名公开消息。分片对象写入、PostgreSQL `InProgress` 条件记录、缓存、指标、HTTP 状态、失败后重试复用和持久清理收敛保持不变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：精确检出 2 处 `.what()`，且两条固定完整语句计数均为 0、预期均为 1；两个既有 `InternalError` 公开消息断言通过。实现后定向测试 1/1、上传仓储合同/真实上传与授权/上传安全网/分布式拓扑聚焦 CTest 18/18（129.45 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1497 项：1490 项通过、7 项按环境门控跳过、0 失败，总耗时 542.37 秒。`UploadService.cpp` 中 `.what()` 为 0，两条目标固定消息各精确出现 1 次。该批不改变公开错误、对象写入、条件记录、缓存、指标、重试或清理行为，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.159 FileController 删除异常脱敏记录（2026-08-03）

API、分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一文件软删除 Controller 的最终异常合同。删除服务抛出的标准异常只允许固定完整消息 `Unexpected exception in delete file` 与调用方既有类型化上下文；异常正文、user/file/folder ID、SQL、连接信息、路径、凭据和 token 不得进入失败消息。

catch 继续返回既有 `InternalError` 与 `Internal error during file deletion`。DTO 校验、`FileMutationService::Delete` 委托、`TrashService::MoveToTrash` 事务、回滚、领域失败和成功响应保持不变。

旧实现上的定向源码合同按预期为 0/1，共 2 个失败断言：mutation Controller 区间精确检出 1 处 `.what()`，且固定完整语句计数为 0、预期为 1；既有 `InternalError` 公开消息断言通过。实现后定向测试 1/1、FileMutation 合同/真实复制删除原子性/文件变更与删除回归/分布式拓扑聚焦 CTest 16/16（8.28 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1497 项：1490 项通过、7 项按环境门控跳过、0 失败，总耗时 543.14 秒。`src/controllers/` 中 `.what()` 为 0，固定失败消息与公开错误各精确出现 1 次。该批不改变 DTO、服务委托、回收站事务、回滚、错误或成功响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.160 StorageFactory S3 初始化错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一存储工厂异常合同。S3 client factory、bucket 可访问性校验或适配器构造异常只允许重新抛出固定完整文本 `Failed to initialize S3 storage backend`；provider `ErrorInfo.message`、标准异常正文、bucket、endpoint、region、prefix/key、路径、凭据和 token 不得进入最终异常链。

外层 `storage_runtime`/bootstrap 固定失败摘要与退出码 1 保持不变。local/S3 选择、bucket 校验调用、共享 S3 adapter 构造、队列/指标注册和初始化顺序保持不变。

旧实现上的两个定向测试按预期为 0/2，共 3 个失败断言：源码精确检出 1 处 `.what()`、缺失固定 throw，行为测试直接观察到注入的 `access denied` 被传播。实现后定向测试 2/2、StorageFactory/观察 Worker/安全 local staging 截止/S3 生命周期/分布式拓扑聚焦 CTest 8/8（2.55 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1497 项：1490 项通过、7 项按环境门控跳过、0 失败，总耗时 544.14 秒。源码审计确认 `StorageFactory.cpp` 中 `.what()` 为 0，固定 throw 精确出现 1 次，注入 provider 文本只存在于测试夹具。该批不改变后端选择、bucket 校验、构造顺序、日志或退出码，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.161 StorageJobAdminService 列表与详情异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一存储任务管理服务异常合同。列表与详情 catch 分别只允许固定完整消息 `Storage job admin list failed` 和 `Storage job admin detail failed`；PostgreSQL/行解析/标准异常正文、SQL、连接信息、job ID、payload、对象定位符、凭据和 token 不得进入日志消息。

详情的已验证持久任务 ID 继续只由类型化 `job_id` 字段承载，列表保持 `job_id=null`。既有 `InternalError`、`Failed to list storage jobs`/`Failed to get storage job` 公开消息、分页、详情、重放事务和审计语义保持不变。

旧实现上的定向合同按预期为 0/1，共 3 个失败断言：服务源码精确检出 2 处 `.what()`，两个固定完整消息均缺失；两个既有公开错误消息断言通过。实现后定向合同 1/1、存储任务管理源码合同/DTO/真实操作流/分布式拓扑聚焦 CTest 13/13（4.18 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1497 项：1490 项通过、7 项按环境门控跳过、0 失败，总耗时 545.33 秒。源码审计确认 `StorageJobAdminService.cpp` 中 `.what()` 为 0，两个固定服务消息与两个公开错误消息均各精确出现 1 次。该批不改变查询、分页、类型化关联、错误映射、重放事务或审计，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.162 StorageReconciliationService 分页异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一存储对账分页异常合同。数据库异常与其他标准异常 catch 分别只允许固定完整消息 `Storage reconciliation database failure` 和 `Storage reconciliation failed`；scope、scan ID、异常正文、SQL、连接信息、对象定位符、凭据和 token 不得进入日志消息。

已认领任务的 `job_id`、`lease_owner` 与固定 operation 继续仅由调用方类型化上下文承载。既有 `InternalError` 公开消息、四类 scope、游标分页、finding 持久化/消解、Blob GC 修复入队和 Worker 重试行为保持不变。

旧实现上的定向合同按预期为 0/1，共 3 个失败断言：服务源码精确检出 2 处 `.what()`，两个固定完整消息均缺失；两个既有公开错误映射断言通过。实现后定向合同 1/1、对账/Worker/真实任务失败持久化/配额安全/分布式拓扑聚焦 CTest 30/30（33.98 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 555.27 秒。源码审计确认 `StorageReconciliationService.cpp` 中 `.what()` 为 0，两个固定服务消息与两个公开错误消息均各精确出现 1 次。该批不改变类型化关联、错误映射、分页、finding、修复入队或 Worker 重试，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.163 ScheduledTasks 计划构建错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一周期计划构建错误合同。`BuildPeriodicSeedPlan` 捕获标准异常后只允许返回固定完整错误 `Failed to build periodic storage job plan`，不得把 `std::exception::what()` 或运行库诊断放入 `expected` 错误值。

`SeedOnce` 继续把计划失败映射为既有 `InternalError` 与 `Failed to build periodic storage jobs`，外层周期失败日志保持固定。UTC 小时/日 scan ID、六个首页任务、payload、去重键、首次立即播种、60 秒周期、Worker 角色归属和排空语义保持不变。

旧实现上的定向合同按预期为 0/1，共 2 个失败断言：源码精确检出 1 处 `.what()`，固定计划错误返回缺失；既有 `SeedOnce` 错误映射断言通过。实现后定向合同 1/1、周期计划/真实角色切换/任务队列/Worker drain/分布式拓扑聚焦 CTest 9/9（35.74 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 559.27 秒。源码审计确认 `ScheduledTasks.cpp` 中 `.what()` 为 0，固定计划错误与既有 `SeedOnce` 错误映射各精确出现 1 次。该批不改变 UTC 窗口、计划内容、错误映射、播种、去重、角色归属或排空行为，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.164 StorageRecoveryAdminService inspection 异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一存储恢复管理 inspection 异常合同。租约 dry-run、cleanup rebuild dry-run 与对账入队 inspection 标准异常 catch 分别只允许固定完整消息 `Upload lease release dry-run failed`、`Upload cleanup rebuild dry-run failed` 和 `Storage reconciliation enqueue inspection failed`；upload/scan ID、异常正文、SQL、连接信息、对象定位符、凭据和 token 不得进入日志消息。

已验证 upload ID 继续只由类型化上下文承载。三个既有 `InternalError` 公开消息、实际变更事务、CAS、去重、审计和响应语义保持不变。

旧实现上的定向合同按预期为 0/1，共 4 个失败断言：源码精确检出 3 处 `.what()`，三个固定完整消息均缺失；三个既有公开错误消息断言通过。实现后定向合同 1/1、恢复管理源码合同/DTO/真实操作流/任务失败持久化/分布式拓扑聚焦 CTest 11/11（13.61 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 542.69 秒。源码审计确认 `StorageRecoveryAdminService.cpp` 中 `.what()`、`failed:` 与 `error=` 均为 0，三个固定服务消息与三个公开错误消息均各精确出现 1 次。该批不改变类型化关联、错误映射、实际变更事务、CAS、去重、审计或响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.165 FolderService 查询数据库异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一文件夹查询数据库异常合同。目录树查询、父目录所有权查询与面包屑查询的 `DrogonDbException` catch 分别只允许固定完整消息 `Folder tree query failed`、`Parent folder ownership lookup failed` 和 `Breadcrumb query failed`；不得拼接 folder/parent/user ID、异常正文、SQL、连接信息、路径、凭据或 token。

请求关联继续只使用调用方现有的类型化上下文。目录树和面包屑的既有 `InternalError` 公开消息、父目录数据库异常的 `FolderNotFound` 映射、查询、树构造、响应和缓存行为保持不变。

旧实现上的定向合同按预期为 0/1，共 4 个失败断言：源码精确检出 3 处 `.what()`，三个固定完整消息均缺失；三条既有公开错误映射断言通过。实现后定向合同 1/1、文件夹源码/仓储合同/真实生命周期/浏览突发/上传安全不变量/分布式拓扑聚焦 CTest 14/14（143.25 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 546.03 秒。源码审计确认 `FolderService.cpp` 中 `.what()` 为 0，三个固定服务消息与目录树/面包屑两个公开错误消息均各精确出现 1 次，父目录目标 catch 仍映射 `FolderNotFound`。该批不改变类型化关联、查询、树构造、缓存、错误映射或响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.166 FileQueryService 数据库异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行统一文件查询异常合同。列表查询、下载元数据更新与搜索的 `DrogonDbException` catch 分别只允许固定完整消息 `File list query failed`、`File download metadata update failed` 和 `File search failed`；不得拼接 file/user/folder ID、异常正文、SQL、连接信息、路径、搜索词、凭据或 token。

列表查询继续返回既有 `InternalError` 与 `Failed to query file list`；下载元数据更新继续 best-effort 吞错，搜索异常继续返回既有默认响应。查询、缓存、下载响应、搜索分页和类型化关联保持不变。

旧实现上的定向合同按预期为 0/1，共 4 个失败断言：源码精确检出 3 处 `.what()`，三个固定完整消息均缺失；列表公开错误、元数据更新吞错与搜索默认响应断言通过。实现后定向合同 1/1、文件查询源码/缓存世代/真实下载/列表与搜索/浏览突发/上传安全不变量/分布式拓扑聚焦 CTest 11/11（144.55 秒）、完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 548.34 秒。源码审计确认 `FileQueryService.cpp` 中 `.what()` 为 0，三个固定服务消息与列表公开错误消息均各精确出现 1 次。该批不改变类型化关联、查询、缓存、下载元数据 best-effort 更新、搜索分页、错误映射或响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.167 TransactionRunner 异常日志脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧共享事务边界。数据库异常与标准异常的 `Run()` catch 只允许固定完整消息 `Database transaction failed`；`rollbackQuietly()` 的 rollback 异常只允许 `Transaction rollback failed`。不得转发异常正文、SQL、连接信息、事务对象、业务标识、路径、凭据或 token。

调用方六个类型化关联字段继续按值原样保留。callback 领域错误原样返回、数据库/标准异常映射调用方默认错误、rollback 失败不覆盖原结果、commit callback 等待、持久 owner 拒绝提交和既有固定提交诊断均保持不变。

旧实现上的定向内存日志合同按预期为 0/1，共 6 个失败断言：两条固定完整消息均未精确出现，rollback、数据库和标准异常三类 fake 实现细节均泄露，默认空上下文消息也携带异常正文；错误映射、回滚和六个关联字段断言通过。实现后定向合同和事务全分支 10/10 通过。首次聚焦 CTest 唯一失败是真实故障注入仍查找旧 PostgreSQL 异常正文；更新为精确查找固定事务消息并解析 application 事件拒绝故障正文后，安全网 1/1（121.70 秒）与最终事务/缓存/文件/文件夹/回收站/存储任务/上传安全网/分布式拓扑聚焦 CTest 20/20（136.97 秒）通过。

完整构建、Python 语法、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1498 项：1491 项通过、7 项按环境门控跳过、0 失败，总耗时 547.62 秒。源码审计确认 `TransactionRunner.hpp` 中 `.what()` 为 0，`Database transaction failed` 固定字面量精确出现 2 次，`Transaction rollback failed` 精确出现 1 次；内存与真实故障日志均拒绝异常正文。该批不改变类型化关联、错误映射、rollback、commit 或业务响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.168 FileMutationService 复制异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧文件复制边界。显式文件批量读取、文件名冲突查询、文件内容批查、文件夹名冲突查询、文件夹内容批查和复制文件批量插入的六个数据库异常 catch 只允许对应固定完整摘要；不得转发异常正文、SQL、连接信息、file/folder/content/user ID、名称、路径、配额、凭据或 token。

查询失败继续按既有批次/目录跳过语义处理，并在该阶段已经预留配额时释放对应预留；批量插入失败继续返回既有 `InternalError` 与 `Failed to insert copied files` 给内部事务，外层仍按批量部分成功合同回滚该批、释放预留并返回成功信封。类型化关联、名称锁、引用计数、item count、配额、缓存失效、响应计数与重试语义不得改变。

旧实现上的定向源码合同按预期为 0/1，共 7 个失败断言：精确检出 6 处 `.what()`，六条固定完整摘要均缺失；既有 `Failed to insert copied files` 内部错误断言通过。实现后复制源码/上下文合同 6/6、真实内容配额安全网 1/1（24.55 秒）和复制/缓存/原子性/路径/配额/分布式拓扑聚焦 CTest 21/21（33.22 秒）通过。

完整构建、Python 语法、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1499 项：1492 项通过、7 项按环境门控跳过、0 失败，总耗时 541.67 秒。源码审计确认 `FileMutationService.cpp` 中 `.what()` 为 0，六条固定摘要各精确出现 1 次；真实 PostgreSQL 插入故障事件保留 response 同一 request/instance/`file_mutation` 与四个空所有权字段，且应用日志拒绝触发器异常正文。该批不改变跳过、事务回滚、预留释放、部分成功响应、类型化关联、名称锁、引用计数、item count、配额、缓存或重试语义，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.169 UploadLifecycle finalize 辅助错误脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧上传完成辅助边界。完成租约续租异常、完成错误 best-effort 记录异常、staging mismatch finding 持久化异常、对账任务构建错误和对账任务入队异常的五条事件只允许固定完整摘要；不得在 message 中重复 upload/lease/version、异常或领域错误正文、SQL、连接信息、对象定位符、凭据或 token。调用方 request/instance/`upload_complete` 与已经权威取得的 upload/lease/version 继续只由类型化字段承载。

续租异常继续返回既有 `InternalError` 与 `Failed to renew upload finalize lease`；错误记录、finding 持久化和任务入队继续 fail-open，任务构建失败继续结束当前对账触发 helper。finding 恢复详情、对账任务 payload/dedupe、CAS、错误码、完成响应与重试语义不得改变。

旧实现上的定向源码合同按预期为 0/1，共 7 个失败断言：finalize helper 区间精确检出 4 处 `.what()` 和 1 处任务构建领域错误转发，五条固定完整日志语句均缺失；既有续租 `InternalError` 断言通过。实现后 helper/finalize/CAS 定向测试 6/6、真实上传安全网 1/1（121.70 秒）和上传 Lifecycle/仓储/状态机/流程/授权/安全网/分布式拓扑聚焦 CTest 34/34（131.83 秒）通过。

完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1500 项：1493 项通过、7 项按环境门控跳过、0 失败，总耗时 542.14 秒。源码审计确认目标 helper 区间 `.what()` 与 `reconciliation_job.error()` 均为 0，五条固定日志语句各精确出现 1 次；全文件仍有 6 处位于该区间之外的异常正文转发，留给后续独立批次。该批不改变类型化关联、续租错误、best-effort、早退、finding、任务、CAS、响应或重试语义，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.170 UploadLifecycle 读取异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧上传完成读取边界。文件名预检、完成租约认领、完成结果重放、staging 会话读取、组装分片描述读取和 finalize 元数据查询的六条异常事件只允许对应固定完整摘要；不得在 message 中重复 upload/user/file/folder、lease/version、异常正文、SQL、连接信息、对象定位符、凭据或 token。

文件名预检与 finalize 元数据查询异常继续按既有 `false`/当前 lookup 降级；租约认领与完成结果读取异常继续返回既有同名 `InternalError`；staging 会话与分片描述读取异常继续记录 finalize error 并返回既有固定错误。类型化关联、计时、CAS、组装、去重、冲突、响应与重试语义不得改变。

旧实现上的定向源码合同按预期为 0/1，共 7 个失败断言：全文件精确检出剩余 6 处 `.what()`，六条固定完整日志语句均缺失；认领、完成重放、staging 会话和分片描述四条既有公开错误断言通过。实现后读取异常/finalize/CAS 定向测试 7/7、上传 Lifecycle/仓储/状态机/流程/授权/安全网/分布式拓扑聚焦 CTest 35/35（131.70 秒，其中上传安全网 121.69 秒）通过。

完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1501 项：1494 项通过、7 项按环境门控跳过、0 失败，总耗时 547.15 秒。源码审计确认 `UploadLifecycleService.cpp` 中 `.what()` 为 0，六条固定日志语句各精确出现 1 次。该批不改变类型化关联、降级值、错误映射、finalize error 记录、计时、CAS、组装、去重、冲突、响应或重试语义，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.171 TrashService 过期单页清理异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧过期回收站单页清理边界。数据库异常与其他标准异常必须分别只记录固定完整摘要 `Database error cleaning expired trash page` 与 `Failed to clean expired trash page`；不得在 message 中追加 cursor/limit/trash/content、异常正文、SQL、连接信息、对象定位符、凭据或 token。

两类异常继续返回既有 `InternalError` 与 `Failed to clean expired trash`；分页边界、候选读取、分块永久删除、无效内容引用检测、计数、cursor 推进、类型化关联和清理组合语义不得改变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：目标方法精确检出 2 处 `.what()`，两条固定完整日志语句均缺失；既有公开错误断言通过。实现后 Trash 定向合同 3/3、Trash/CleanupService/生命周期/上传与配额安全网/分布式拓扑聚焦 CTest 82/82（151.68 秒，其中上传安全网 121.69 秒、配额安全网 25.54 秒）通过。

完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1502 项：1495 项通过、7 项按环境门控跳过、0 失败，总耗时 544.39 秒。源码审计确认 `CleanupExpiredTrashPage()` 中 `.what()` 为 0，两条固定日志语句各精确出现 1 次；`TrashService.cpp` 其余路径仍有 10 处 `.what()`，留待后续独立批次处理。本批不改变分页、分块删除、无效引用检测、计数、cursor、类型化关联、公开错误或清理组合语义，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.172 TrashService 列表计数异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧回收站列表和计数读取边界。`List()` 的数据库异常与其他标准异常必须分别只记录固定完整摘要 `Database error fetching trash list` 与 `Unknown error fetching trash list`，`Count()` 的数据库异常只记录固定 `Failed to count trash items`；不得在 message 中追加 user/page/page_size、异常正文、SQL、连接信息、路径、凭据或 token。

列表异常继续返回既有 `InternalError` 与 `Failed to fetch trash list, please try again later`，计数异常继续返回既有 `InternalError` 与 `Failed to count trash items`。用户范围、分页、排序、响应映射、计数查询、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 4 个失败断言：目标范围精确检出 3 处 `.what()`，三条固定完整日志语句均缺失；列表与计数既有公开错误断言通过。实现后 Trash 定向合同 4/4、Trash 单元/DTO/真实生命周期/分布式拓扑聚焦 CTest 67/67（4.47 秒）通过。

完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1503 项：1496 项通过、7 项按环境门控跳过、0 失败，总耗时 551.50 秒。源码审计确认 `List()`/`Count()` 目标范围 `.what()` 为 0，三条固定日志语句各精确出现 1 次；`TrashService.cpp` 其余路径仍有 7 处 `.what()`，留待后续独立批次处理。本批不改变用户范围、分页、排序、响应映射、计数查询、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.173 TrashService 批量预取异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧回收站恢复/永久删除的批量预取边界。两条数据库异常必须分别只记录固定完整摘要 `Failed to batch fetch trash items for restore` 与 `Failed to batch fetch trash items for delete`；不得在 message 中追加 user/trash ID、异常正文、SQL、连接信息、路径、凭据或 token。

恢复预取异常继续返回既有 `InternalError` 与 `Failed to restore trash items, please try again later`，删除预取异常继续返回既有 `InternalError` 与 `Failed to delete trash items, please try again later`。单次批量预取、快照所有权判定、输入顺序、缺失/跨用户逐项结果、恢复/删除事务、部分成功、缓存、引用计数、配额、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 4 个失败断言：两个预取块各精确检出 1 处 `.what()`，两条固定完整日志语句均缺失；恢复/删除既有公开错误断言通过。实现后 Trash 定向合同 5/5、Trash 单元/DTO/批处理刻画/真实生命周期/配额安全网/分布式拓扑聚焦 CTest 69/69（28.86 秒，其中配额安全网 24.55 秒）通过。

首次完整 CTest 的 `SafetyContentQuotaIntegration` 在 DeleteAll 最终内容行收敛处出现 1 个未稳定复现的失败，前 400 个断言通过；该测试随即单独复跑 1/1（25.53 秒）通过。随后标准完整 CTest 重跑共 1504 项：1497 项通过、7 项按环境门控跳过、0 失败，总耗时 545.54 秒。完整构建、OpenSpec 24/24 和差异检查通过。源码审计确认两个预取块 `.what()` 均为 0，两条固定日志语句各精确出现 1 次；`TrashService.cpp` 其余路径仍有 5 处 `.what()`，留待后续独立批次处理。本批不改变单次预取、快照授权、输入顺序、逐项结果、事务、部分成功、缓存、引用计数、配额、类型化关联或公开错误，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.174 TrashService 永久删除异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧回收站永久删除链的剩余异常边界。逐项删除、DeleteAll 分块、DeleteAll 外层数据库/标准异常和事务回滚必须分别只记录固定完整摘要 `Failed to permanently delete trash item`、`Failed to process DeleteAll chunk atomically`、`Database error emptying trash`、`Unknown error emptying trash` 与 `Trash permanent-delete rollback failed`；不得在 message 中追加 user/trash/content ID、异常正文、SQL、连接信息、路径、凭据或 token。

逐项异常继续生成既有 item-type 固定失败结果；DeleteAll 分块异常继续跳过该块并处理后续块，外层异常继续返回既有 `InternalError` 与 `Failed to empty trash, please try again later`；回滚异常继续不覆盖并重抛原失败。输入顺序、分块边界、部分成功、删除计数、释放空间、事务、缓存、引用计数、Blob GC、配额、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 9 个失败断言：全服务精确检出剩余 5 处 `.what()`，逐项删除/DeleteAll/永久删除三个目标范围分别检出 1/3/1 处，五条固定完整日志语句均缺失；逐项/外层公开错误和回滚重抛断言通过。实现后 Trash 定向合同 6/6、Trash 单元/DTO/批处理刻画/真实生命周期/配额安全网/分布式拓扑聚焦 CTest 70/70（28.77 秒，其中配额安全网 24.50 秒）通过。

完整构建、OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1505 项：1498 项通过、7 项按环境门控跳过、0 失败，总耗时 548.28 秒。源码审计确认 `TrashService.cpp` 中 `.what()` 为 0，五条固定日志语句各精确出现 1 次。本批不改变逐项固定失败、分块继续、外层错误、回滚重抛、输入顺序、分块、部分成功、计数、释放空间、事务、缓存、引用计数、Blob GC、配额、类型化关联或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.175 ShareService 列表查询异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享列表读取边界。总数查询与分页查询的数据库异常必须分别只记录固定完整摘要 `Failed to get share count` 与 `Failed to get share list`；不得在 message 中追加 user/status/page/page_size、异常正文、SQL、连接信息、分享码、凭据或 token。

两条异常继续返回既有 `InternalError` 与 `Failed to get share list`。用户范围、active 状态的过期二次过滤、其他状态筛选、创建时间倒序分页、批量关联文件加载、响应映射、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：`List()` 精确检出 2 处 `.what()`，两条固定完整日志语句均缺失；两处既有公开错误断言通过。实现后 Share 定向合同 6/6、Share 单元/DTO/查询/管理/碰撞/审计/Token/限流/分布式拓扑聚焦 CTest 148/148（54.26 秒）通过。

完整构建确认 `ShareService.cpp` 实际重新编译，OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1506 项：1499 项通过、7 项按环境门控跳过、0 失败，总耗时 542.67 秒。源码审计确认 `List()` 中 `.what()` 为 0，两条固定日志语句各精确出现 1 次；`ShareService.cpp` 其余路径仍有 20 处 `.what()`，留待后续独立批次处理。本批不改变用户范围、状态/过期过滤、倒序分页、关联文件加载、响应映射、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.176 ShareService 创建事务异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享创建事务边界。创建事务数据库异常与随后回滚异常必须分别只记录固定完整摘要 `Failed to create share (transaction)` 与 `Transaction rollback failed`；不得在 message 中追加 user/file/folder/share ID、候选分享码、异常正文、SQL、连接信息、密码、凭据或 token。

数据库异常继续返回既有 `InternalError` 与 `Failed to create share`，回滚异常继续不覆盖原失败。所有权校验、密码哈希、最多 5 次数据库唯一冲突重试、分享及关联行原子写入、碰撞耗尽回滚、显式提交、提交后 fail-open 审计、响应映射、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：`Create()` 精确检出 2 处 `.what()`，两条固定完整日志语句均缺失；3 处既有公开错误和 2 次回滚调用断言通过。实现后 Share 定向合同 7/7、Share 单元/DTO/查询/管理/碰撞/审计/Token/限流/分布式拓扑聚焦 CTest 149/149（42.93 秒）通过。

完整构建确认 `ShareService.cpp` 实际重新编译，OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1507 项：1500 项通过、7 项按环境门控跳过、0 失败，总耗时 548.68 秒。源码审计确认 `Create()` 中 `.what()` 为 0，两条固定日志语句各精确出现 1 次；`ShareService.cpp` 其余路径仍有 18 处 `.what()`，留待后续独立批次处理。本批不改变校验、哈希、碰撞重试、关联写入、回滚、提交、审计、响应、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.177 ShareService 管理写入异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享更新及批量取消边界。设置更新数据库异常、取消分块预取异常与取消批量更新异常必须分别只记录固定完整摘要 `Failed to update share`、`Failed to fetch shares for cancel` 与 `Failed to cancel share`；不得在 message 中追加 user/share ID、分块内容、异常正文、SQL、连接信息、密码、凭据或 token。

更新异常继续返回既有 `InternalError` 与 `Failed to update share`。取消预取失败继续把当前分块全部映射为既有 `Operation failed/internal_error` 并逐项审计后处理后续分块；取消更新失败继续只把当前分块中原计划成功的项目改为同一固定失败。所有权/活动状态校验、可选过期时间/密码/权限更新、密码哈希、输入顺序、分块、重复项规则、部分成功、计数、审计、响应映射、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 5 个失败断言：`Update()` 精确检出 1 处、`Cancel()` 精确检出 2 处 `.what()`，三条固定完整日志语句均缺失；更新公开错误、取消两处固定逐项失败、成功计数回退和两处审计调用断言通过。实现后 Share 定向合同 8/8、Share 单元/DTO/查询/管理/碰撞/审计/Token/限流/分布式拓扑聚焦 CTest 150/150（43.62 秒）通过。

完整构建确认 `ShareService.cpp` 实际重新编译，OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1508 项：1501 项通过、7 项按环境门控跳过、0 失败，总耗时 544.68 秒。源码审计确认 `Update()` 与 `Cancel()` 中 `.what()` 均为 0，三条固定日志语句各精确出现 1 次；`ShareService.cpp` 其余路径仍有 15 处 `.what()`，留待后续独立批次处理。本批不改变校验、可选字段更新、密码哈希、输入顺序、分块、重复项、部分成功、计数、审计、响应、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.178 ShareService 公开读取异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享文件夹浏览及下载元数据读取边界。文件夹浏览数据库异常与下载元数据数据库异常必须分别只记录固定完整摘要 `Failed to browse share folder` 与 `Failed to get download info`；不得在 message 中追加 share/file/folder/user ID、异常正文、SQL、连接信息、存储定位符、凭据或 token。

两条异常继续分别返回既有 `InternalError` 与 `Failed to browse share content`/`Failed to get download info`。分享活动状态校验、根目录路径、共享文件夹访问谓词、子文件/文件夹排序、面包屑、下载元数据单次四表 JOIN、分享状态/过期/成员关系/下载权限校验、Blob 映射、Range 能力、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 4 个失败断言：`Browse()` 与 `GetDownloadInfo()` 各精确检出 1 处 `.what()`，两条固定完整日志语句均缺失；两处既有公开错误、两个共享文件夹访问谓词和单次共享文件访问谓词断言通过。实现后 Share 定向合同 9/9、Share 单元/DTO/查询/管理/碰撞/审计/Token/限流/分布式拓扑聚焦 CTest 151/151（43.06 秒）通过。

完整构建确认 `ShareService.cpp` 实际重新编译，OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1509 项：1502 项通过、7 项按环境门控跳过、0 失败，总耗时 555.92 秒。源码审计确认 `Browse()` 与 `GetDownloadInfo()` 中 `.what()` 均为 0，两条固定日志语句各精确出现 1 次；`ShareService.cpp` 其余路径仍有 13 处 `.what()`，留待后续独立批次处理。本批不改变活动状态、根目录、访问谓词、排序、面包屑、单次 JOIN、成员/权限校验、Blob/Range 映射、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.179 ShareService 保存事务异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享保存到网盘的事务补偿边界。数据库异常与标准异常必须只记录固定完整摘要 `Failed to save share items`，两类失败随后各自的回滚异常必须只记录固定完整摘要 `Transaction rollback failed`；不得在 message 中追加 share/file/folder/user/content ID、异常正文、SQL、连接信息、路径、存储定位符、凭据或 token。

两类异常继续返回既有 `InternalError` 与 `Failed to save share items`，回滚异常继续不覆盖原失败。分享状态/权限、目标文件夹归属、共享成员校验、递归复制计划、冲突跳过、配额预留及按实际保存量退款、内容引用计数、路径/层级、项目计数、父目录计数、显式提交、成功后缓存失效、响应映射、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：`SaveToDrive()` 精确检出 4 处 `.what()`，两类固定完整日志语句均缺失；3 处既有公开错误、3 次回滚、单次提交、两处引用递增和成功后一次缓存失效断言通过。实现后 Share 定向合同 10/10、Share 单元/DTO/查询/管理/碰撞/审计/Token/限流/分布式拓扑聚焦 CTest 152/152（43.00 秒）通过。

完整构建确认 `ShareService.cpp` 实际重新编译，OpenSpec 24/24 和差异检查通过。标准完整 CTest 共 1510 项：1503 项通过、7 项按环境门控跳过、0 失败，总耗时 548.49 秒。源码审计确认 `SaveToDrive()` 中 `.what()` 为 0，保存失败与回滚失败两条固定日志语句各精确出现 2 次；`ShareService.cpp` 其余路径仍有 9 处 `.what()`，留待后续独立批次处理。本批不改变校验、递归计划、冲突、配额、引用、路径、计数、回滚、提交、缓存、响应、类型化关联、公开错误或 Controller 组合，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.180 ShareService 私有 helper 异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧分享私有查询、校验及统计 helper 边界。分享查找、文件/文件夹所有权校验、单项/批量分享文件加载、活动状态校验、浏览/下载计数更新及共享文件下载元数据更新异常必须只记录各自既有前缀的固定完整摘要；不得在 message 中追加 share/file/folder/user ID、异常正文、SQL、连接信息、路径、存储定位符、凭据或 token。

分享查找及三类校验继续返回既有业务/内部错误；单项/批量分享文件加载继续 fail-open 返回已构建或空结果；三类统计更新继续 fail-open。唯一行/缺失映射、所有权输入去重及原顺序恢复、两次 JOIN、跨文件/文件夹关联顺序、批次分块、活动状态、计数 SQL、下载元数据字段、调用方结果、类型化关联及 Controller 组合不得改变。

旧实现上的定向源码合同按预期为 0/1，共 10 个失败断言：私有 helper 尾部精确检出 9 处 `.what()`，8 类固定完整摘要缺失，且共享文件下载元数据日志仍追加 `file_id`；4 处既有公开错误映射断言通过。实现后 Share 定向合同 11/11、Share 与分布式拓扑聚焦 CTest 153/153（42.72 秒）、OpenSpec 24/24 通过；完整构建确认 `ShareService.cpp` 实际重新编译并链接。标准完整 CTest 共 1511 项：1504 项通过、7 项按环境门控跳过、0 失败，总耗时 544.01 秒。源码审计确认整个 `ShareService.cpp` 已无 `.what()` 或 `(file_id=` 日志拼接，7 类单点摘要各精确出现 1 次，分享文件列表摘要精确出现 2 次。本批不改变上述业务语义，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.181 AdminService 用户列表查询异常脱敏记录（2026-08-03）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧管理员用户列表诊断边界。`ListUsers()` 的计数或分页查询遇到数据库异常时，只允许记录固定完整摘要 `Admin list users database error`；message 不得追加管理员/用户 ID、筛选或分页值、异常正文、SQL、连接信息、凭据或 token。

该异常继续返回既有 `InternalError` 与 `Failed to list users`。用户名/邮箱/状态/角色筛选、总数查询、创建时间倒序分页、可空字段映射、分页元数据、request/instance/`admin` 类型化关联、公开响应及 Controller 组合不得改变。

PostgreSQL 将 `LIMIT $1 OFFSET $2` 推断为 `bigint` 参数，因此 `page_size` 与 `offset` 必须与仓内其他分页查询一致显式绑定为 `int64_t`；禁止以 4 字节 `int` 二进制参数触发 `int8` 解码失败。这只修正绑定宽度，不改变 SQL、页码算法或响应数值。

`test_admin_flow.py` 的普通用户与软删除用户夹具必须直接使用带短前缀的 `unique_name()`，在保留 PID+毫秒唯一性的同时满足注册用户名 4–32 字符合同；不得再叠加第二层前缀导致真实管理流在 setup 阶段失效。

旧日志实现上的定向源码合同按预期为 0/1，共 2 个失败断言：`ListUsers()` 精确检出 1 处 `.what()` 且固定完整摘要缺失；既有公开错误、两次查询和倒序分页断言通过。首次真实管理流因夹具叠加前缀超过 32 字符，注册返回 `400/10002`、后续登录返回 404；缩短夹具后 setup 通过，用户列表暴露 `LIMIT/OFFSET` 以 4 字节 `int` 绑定导致的 `500/Failed to list users`。补充绑定合同后旧实现再按预期为 0/1，唯一失败断言精确检出 `int64_t` 转换 0/2，其余断言通过。

实现后 Admin 定向合同 3/3、管理员单元/DTO/过滤器/分布式拓扑聚焦 CTest 59/59（1.70 秒）、Python 语法、OpenSpec 24/24 和差异检查通过；完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流的用户列表、搜索、详情、状态/角色变更、软删除、两项自保护与两项鉴权共前 10 场景通过；第 11 场景在本批范围外的 `ListShares()` 旧分页边界返回 `500/Failed to list shares`，留待 15.182 独立处理。标准完整 CTest 共 1512 项：1505 项通过、7 项按环境门控跳过、0 失败，总耗时 561.13 秒。源码审计确认 `ListUsers()` 中 `.what()` 为 0、固定摘要精确出现 1 次、`int64_t` 转换精确出现 2 次；`AdminService.cpp` 其余路径仍有 22 处 `.what()`。本批不改变筛选、SQL、页码算法、字段映射、类型化关联、公开错误或响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.182 AdminService 分享列表查询异常脱敏记录（2026-08-04）

分布式 ADR、部署、系统测试、单元测试与 OpenSpec 先行收紧管理员分享列表诊断边界。`ListShares()` 的空分享停用、计数或分页查询遇到数据库异常时，只允许记录固定完整摘要 `Admin list shares database error`；message 不得追加管理员/分享/用户/文件 ID、筛选或分页值、异常正文、SQL、连接信息、凭据或 token。

该异常继续返回既有 `InternalError` 与 `Failed to list shares`。无文件活动分享停用、状态/用户 ID/用户名筛选、参数化用户名模糊匹配、总数查询、创建时间倒序分页、首个分享文件映射、可空字段映射、分页元数据、操作审计、request/instance/`admin` 类型化关联、公开响应及 Controller 组合不得改变。

有无用户名筛选的两条 PostgreSQL 分页分支都必须将 `LIMIT/OFFSET` 显式绑定为 `int64_t`，禁止以 4 字节 `int` 二进制参数触发 `int8` 解码失败。这只修正绑定宽度，不改变占位符编号、SQL、页码算法或响应数值。

分页查询返回的 PostgreSQL `boolean password_set` 必须按仓内既有方言直接使用 `as<bool>()` 读取；禁止按 `int` 解析 `t/f` 文本并抛出未捕获的 `std::invalid_argument("stoi")`。该修复不改变 `password_set` 的 JSON 布尔类型或真假语义，分享详情的同类旧映射留待独立批次。

旧实现上的首轮定向源码合同按预期为 0/1，共 3 个失败断言：`ListShares()` 精确检出 1 处 `.what()`，固定完整摘要缺失，且 `int64_t` 转换为 0/2；既有公开错误、五个查询调用点、参数化用户名筛选与倒序分页断言通过。修正分页和日志后首次真实管理流仍在第 11 场景返回空响应 500，受管服务日志定位为 PostgreSQL `boolean password_set` 被 `as<int>()` 读取后抛出未捕获的 `std::invalid_argument("stoi")`。补充布尔映射合同后旧映射再按预期为 0/1，共 2 个失败断言：`as<int>()` 为 1/0、`as<bool>()` 为 0/1，其余断言通过。

实现后 Admin 定向合同 4/4、管理员单元/DTO/过滤器/分布式拓扑聚焦 CTest 60/60（1.64 秒）、Python 语法、OpenSpec 24/24 和差异检查通过；完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流前 11 场景通过；第 12 场景将创建接口返回的外部分享标识传给管理员强制取消路径时，被旧数字 ID 校验以 `400/10002 Invalid share id format` 拒绝，留待 15.183 独立处理。标准完整 CTest 共 1513 项：1506 项通过、7 项按环境门控跳过、0 失败，总耗时 565.26 秒。源码审计确认 `ListShares()` 中 `.what()` 为 0、固定摘要精确出现 1 次、`int64_t` 转换精确出现 2 次、`password_set` 的 `as<int>()` 为 0 且 `as<bool>()` 为 1；`AdminService.cpp` 其余路径仍有 21 处 `.what()`。本批不改变停用、筛选、查询、占位符、页码算法、首文件/可空字段映射、审计、类型化关联、公开错误或响应，Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.183 管理员分享外部标识一致性记录（2026-08-04）

API 设计、功能规格、管理员 OpenSpec、分享 OpenSpec、系统/单元测试、桌面权威文档、Web 对接文档与 TUI 端点清单先行统一管理员分享标识。`GET /api/admin/shares`、`GET /api/admin/shares/{share_id}` 与 `DELETE /api/admin/shares/{share_id}` 的路径和响应只允许使用字符串外部标识 `shares.share_code`；禁止向管理员 Web、桌面或 TUI 客户端序列化、要求或传回内部自增主键 `shares.id`。

管理员分享列表/详情 DTO 必须只公开 `share_id`，不得并行保留 `id` 或 `share_code` 兼容字段。详情与强制取消必须按 `share_code` 查找记录；查询得到的内部 ID 只可继续用于 `share_files` 关联、精确状态更新和操作审计。详情响应继续保持 Controller 既有成功信封，但不再额外嵌套 `share` 对象；不存在、已取消、数据库失败及成功语义保持既有错误码和消息。

详情查询返回的 PostgreSQL `boolean password_set` 必须使用 `as<bool>()` 读取。详情与强制取消数据库异常分别只允许固定完整摘要 `Admin get share detail database error` 与 `Admin force cancel share database error`，不得追加管理员/分享/用户/文件 ID、外部标识、异常正文、SQL、连接信息、凭据或 token；request/instance/`admin` 类型化关联保持不变。

Web API/store/view、Qt `AdminShareListModel`/`AdminManager`/QML 与 TUI client/view 必须把管理员分享主键统一建模为字符串 `share_id`，并对路径段进行既有客户端适用的安全编码。不得以数字转换截断、拒绝或替换八字符 ASCII 字母数字分享标识；三端的列表展示、详情打开、复制和强制取消工作流保持不变。

旧后端实现上的源码合同按预期为 0/1，共 20 个失败断言，精确检出 DTO/路由/Controller/Service 仍暴露或要求内部数字 ID、详情额外嵌套、布尔错误映射和两条异常正文日志；旧 Web 定向测试为 0/1，精确检出路径段未编码；Qt 模型测试因缺少字符串 `share_id`/`ShareIdRole` 编译失败；Go 客户端测试因仍要求 `uint64` 且缺少 `ShareID` 编译失败。实现后后端直接合同/DTO 8/8、全部 Admin GoogleTest 69/69、Web 114/114 与类型检查/生产构建、桌面 unit CTest 1/1、管理员分享 Quick 函数 2/2、TUI `go test ./...`、Python 语法、OpenSpec 24/24 和差异检查通过。综合 API 脚本也已删除内部 ID 反查并通过语法/源码审计，但实际运行在任何端点前因固定前置账号 `test001/Test1234` 不存在而停止，不能计为通过。桌面完整 Quick 仍有 473 通过、24 失败、1 跳过，失败位置在未改动的 OwnerShell/SystemTab 等既有合同；管理员页单文件 21 项通过，唯一失败为未改动的 MySQL 状态文案断言，不阻断本批分享函数级 2/2 证据。

真实管理流以创建接口返回的 `nnuckdS5` 依次完成管理员列表匹配、详情直接响应结构断言和强制取消，前 14 场景全部通过；第 15 场景查询不存在用户仍返回 `500/10006 Failed to get user detail`，留待 15.184 独立修复。标准完整 CTest 共 1514 项：1507 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 554.37 秒。该批不改变分享所有者/访客接口、创建/访问/密码/令牌、状态语义、审计内容或错误码；Phase 10 与最终 Definition of Done 继续保持未勾选。

### 15.184 AdminService 用户详情缺失映射修复记录（2026-08-04）

管理员查询不存在的用户详情时，`CoroMapper<Users>::findOne()` 抛出的 `drogon::orm::UnexpectedRows` 必须作为明确的缺失业务分支处理，返回既有 `AdminUserNotFound`（HTTP 404、业务码 80002、`User not found`）；禁止继续依赖第三方异常正文中的 `condition`/`empty` 片段推断缺失状态。该专用捕获必须位于一般 `DrogonDbException` 捕获之前。

真实数据库异常继续返回既有 `InternalError` 与 `Failed to get user detail`，但诊断只允许固定完整摘要 `Admin get user detail database error`；不得追加用户 ID、异常正文、SQL、连接信息、凭据或 token。成功详情字段、路径参数校验、管理员鉴权、request/instance/`admin` 类型化关联、公开错误码和 Controller 组合不得改变。

验证必须新增源码合同锁定专用异常顺序、两类公开错误和固定诊断，收紧真实管理流第 15 场景为精确 `404/80002/User not found`，并执行 Admin 聚焦 GoogleTest、Python 语法、OpenSpec、完整构建及标准完整 CTest。旧实现上的定向源码合同按预期为 0/1，共 8 个失败断言：专用捕获缺失，一般捕获仍绑定异常对象，精确检出 2 处 `.what()`、2 处异常文本猜测且固定数据库摘要缺失；既有两类公开错误断言通过。

实现后直接源码合同 1/1、全部 Admin GoogleTest 70/70、Python 语法和 OpenSpec 24/24 通过，完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流全部 21 个断言组通过、0 失败，其中第 15 场景精确得到 `404/80002/User not found`，后续可用空间与鉴权场景继续通过。标准完整 CTest 共 1515 项：1508 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 552.11 秒。源码审计确认 `GetUserDetail()` 范围内专用捕获与固定数据库摘要各 1 处、`.what()` 和异常文本猜测均为 0；`AdminService.cpp` 其余 17 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.185 AdminService 用户状态缺失映射修复记录（2026-08-04）

管理员修改不存在用户的状态时，`CoroMapper<Users>::findOne()` 抛出的 `drogon::orm::UnexpectedRows` 必须由 `ChangeUserStatus()` 在一般 `DrogonDbException` 之前显式捕获，并返回既有 `AdminUserNotFound`（HTTP 404、业务码 80002、`User not found`）；禁止依赖第三方异常正文中的 `condition`/`empty` 片段推断缺失状态。

真实数据库异常继续返回既有 `InternalError` 与 `Failed to change user status`，诊断只允许固定完整摘要 `Admin change user status database error`；不得追加目标/管理员 ID、状态值、异常正文、SQL、连接信息、凭据或 token。自身修改保护、状态写入、`login_attempts/locked_until` 重置、操作审计、成功响应、DTO 校验、鉴权和类型化关联不得改变。

验证必须新增源码合同锁定专用捕获顺序、两类公开错误、状态/锁定字段更新、审计与固定诊断，并在真实管理流的状态场景中精确断言不存在用户返回 `404/80002/User not found`。旧实现上的定向源码合同按预期为 0/1，共 8 个失败断言：专用捕获缺失，一般捕获仍绑定异常对象，精确检出 2 处 `.what()`、2 处异常文本猜测且固定数据库摘要缺失；状态写入、登录失败计数与锁定截止时间重置、更新、审计和两类公开错误断言通过。

实现后直接源码合同 1/1、全部 Admin GoogleTest 71/71、Python 语法和 OpenSpec 24/24 通过，完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流全部 22 个断言组通过、0 失败，正常状态变更与恢复后，不存在用户的状态变更精确得到 `404/80002/User not found`，后续角色、分享、统计、配额和鉴权场景继续通过。标准完整 CTest 共 1516 项：1509 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 547.75 秒。源码审计确认 `ChangeUserStatus()` 范围内专用捕获与固定数据库摘要各 1 处、`.what()` 和异常文本猜测均为 0；`AdminService.cpp` 其余 15 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.186 AdminService 用户角色缺失与计数诊断修复记录（2026-08-04）

管理员修改不存在用户的角色时，`ChangeUserRole()` 必须在外层一般 `DrogonDbException` 之前显式捕获 `drogon::orm::UnexpectedRows`，并返回既有 `AdminUserNotFound`（HTTP 404、业务码 80002、`User not found`）；禁止依赖第三方异常正文中的 `condition`/`empty` 片段推断缺失状态。

最后管理员计数查询异常继续返回既有 `InternalError` 与 `Failed to verify admin count`，但诊断只允许固定完整摘要 `Admin count administrators database error`。外层真实数据库异常继续返回既有 `InternalError` 与 `Failed to change user role`，诊断只允许固定完整摘要 `Admin change user role database error`。两者均不得追加目标/管理员 ID、角色值、异常正文、SQL、连接信息、凭据或 token。

自身修改保护、仅降级管理员时执行的计数查询、最后管理员保护、角色写入、操作审计、成功响应、DTO 校验、鉴权和类型化关联不得改变。验证必须新增源码合同锁定两个数据库边界、专用捕获顺序、三类公开错误、计数/写入/审计和固定诊断，并在真实管理流角色场景中精确断言不存在用户返回 `404/80002/User not found`。旧实现上的定向源码合同按预期为 0/1，共 9 个失败断言：专用捕获缺失，两个一般捕获仍绑定异常对象，精确检出 3 处 `.what()`、2 处异常文本猜测且两条固定数据库摘要缺失；管理员计数条件/SQL、最后管理员保护、角色更新、审计及三类公开错误断言通过。

实现后直接源码合同 1/1、全部 Admin GoogleTest 72/72、Python 语法和 OpenSpec 24/24 通过，完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流全部 23 个断言组通过、0 失败，不存在用户的角色变更精确得到 `404/80002/User not found`，后续软删除、自身保护、分享、统计、配额和鉴权场景继续通过。标准完整 CTest 共 1517 项：1510 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 548.88 秒。源码审计确认 `ChangeUserRole()` 范围内专用捕获 1 处、一般数据库捕获 2 处、两条固定摘要各 1 处、`.what()` 和异常文本猜测均为 0；`AdminService.cpp` 其余 12 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.187 AdminService 用户可用空间缺失映射修复记录（2026-08-04）

管理员修改不存在用户的可用空间时，`ChangeUserAvailableSpace()` 必须在一般 `DrogonDbException` 之前显式捕获 `drogon::orm::UnexpectedRows`，并返回既有 `AdminUserNotFound`（HTTP 404、业务码 80002、`User not found`）；禁止依赖第三方异常正文中的 `condition`/`empty` 片段推断缺失状态。

真实数据库异常继续返回既有 `InternalError` 与 `Failed to change user available space`，诊断只允许固定完整摘要 `Admin change user available space database error`；不得追加目标/管理员 ID、可用空间/配额值、异常正文、SQL、连接信息、凭据或 token。自身修改保护、GiB 到字节换算与两层溢出校验、`storage_used + storage_reserved + available_space` 配额公式、用户更新、响应字段、`admin.user.available_space_set` 审计、DTO 校验、鉴权和类型化关联不得改变。

验证必须新增源码合同锁定专用捕获顺序、两类公开错误、配额计算/更新/响应/审计和固定诊断，并在真实管理流可用空间场景中精确断言不存在用户返回 `404/80002/User not found`。旧实现上的定向源码合同按预期为 0/1，共 8 个失败断言：专用捕获缺失，一般捕获仍绑定异常对象，精确检出 2 处 `.what()`、2 处异常文本猜测且固定数据库摘要缺失；GiB 换算、两层溢出防护、配额公式、更新、响应、审计和两类公开错误断言通过。

实现后直接源码合同 1/1、全部 Admin GoogleTest 73/73、Python 语法和 OpenSpec 24/24 通过，完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流全部 24 个断言组通过、0 失败，成功配额更新和审计检查后，不存在用户的可用空间变更精确得到 `404/80002/User not found`，后续校验和鉴权场景继续通过。标准完整 CTest 共 1518 项：1511 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 553.40 秒。源码审计确认 `ChangeUserAvailableSpace()` 范围内专用捕获与固定数据库摘要各 1 处、`.what()` 和异常文本猜测均为 0；`AdminService.cpp` 其余 10 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.188 AdminService 软删除用户缺失映射修复记录（2026-08-04）

管理员软删除不存在用户时，`SoftDeleteUser()` 必须在一般 `DrogonDbException` 之前显式捕获 `drogon::orm::UnexpectedRows`，并返回既有 `AdminUserNotFound`（HTTP 404、业务码 80002、`User not found`）；禁止依赖第三方异常正文中的 `condition`/`empty` 片段推断缺失状态。

真实数据库异常继续返回既有 `InternalError` 与 `Failed to soft delete user`，诊断只允许固定完整摘要 `Admin soft delete user database error`；不得追加目标/管理员 ID、用户名、异常正文、SQL、连接信息、凭据或 token。自身删除保护、状态置为 0、用户更新、`admin.user.soft_delete` 审计、成功响应、鉴权和类型化关联不得改变。

验证必须新增源码合同锁定专用捕获顺序、两类公开错误、状态更新/审计和固定诊断，并在真实管理流软删除场景中精确断言不存在用户返回 `404/80002/User not found`。旧实现上的定向源码合同按预期为 0/1，共 8 个失败断言：专用捕获缺失，一般捕获仍绑定异常对象，精确检出 2 处 `.what()`、2 处异常文本猜测且固定数据库摘要缺失；状态置零、更新、审计和两类公开错误断言通过。

实现后直接源码合同 1/1、全部 Admin GoogleTest 74/74、Python 语法和 OpenSpec 24/24 通过，完整构建确认 `AdminService.cpp` 实际重新编译并链接。真实管理流全部 25 个断言组通过、0 失败，成功软删除临时用户后，不存在用户的软删除精确得到 `404/80002/User not found`，后续自身保护、分享、统计、配额和鉴权场景继续通过。标准完整 CTest 共 1519 项：1512 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 565.51 秒。源码审计确认 `SoftDeleteUser()` 范围内专用捕获与固定数据库摘要各 1 处、`.what()` 和异常文本猜测均为 0；`AdminService.cpp` 其余 8 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.189 AdminService 全局存储统计诊断脱敏记录（2026-08-04）

`GetGlobalStorageStats()` 的任一用户、文件或活动分享聚合查询遇到数据库异常时，只允许记录固定完整摘要 `Admin get global storage stats database error`；message 不得追加管理员 ID、聚合值、异常正文、SQL、连接信息、凭据或 token。该异常继续返回既有 `InternalError` 与 `Failed to get global storage stats`。

用户数/已用空间/总配额、文件数和活动分享数三条聚合查询、空结果回退、响应映射、`admin.storage.global_stats` 读取审计、真实管理员操作人、成功响应和类型化关联不得改变。验证必须新增源码合同锁定固定诊断、公开错误、三条查询、字段映射和审计，并执行全部 Admin GoogleTest、OpenSpec、完整构建、真实统计集成覆盖及标准完整 CTest。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：方法范围内仍有 1 处 `.what()`，一般数据库异常捕获仍绑定异常对象，固定完整摘要缺失；三条查询、关键 SQL、五项响应字段、公开错误和读取审计断言均通过。

实现后定向源码合同 1/1、全部 Admin GoogleTest 75/75、OpenSpec 24/24 和完整后端构建通过。真实 `SafetyUploadInvariantsIntegration` 共 888 个断言通过、0 失败，确认全局存储统计、真实管理员读取审计及上传安全不变量不变。标准完整 CTest 共 1520 项：1513 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 549.80 秒。源码审计确认 `GetGlobalStorageStats()` 范围内固定数据库摘要 1 处、`.what()` 为 0；`AdminService.cpp` 其余 7 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.190 AdminService 系统概览诊断脱敏记录（2026-08-04）

`GetOverviewStats()` 的任一用户、文件或活动分享聚合查询遇到数据库异常时，只允许记录固定完整摘要 `admin.stats.overview database error`；message 不得追加聚合值、异常正文、SQL、连接信息、凭据或 token。该异常继续返回既有 `InternalError` 与 `Failed to get overview stats`。

用户数/已用空间/总配额、文件数和活动分享数三条聚合查询、空结果回退、五项响应映射、成功日志、Controller 成功响应、鉴权和类型化请求上下文不得改变。验证必须新增源码合同锁定匿名捕获、固定诊断、公开错误、三条查询、字段映射和成功日志，并执行全部 Admin GoogleTest、OpenSpec、完整构建、真实管理员概览流及标准完整 CTest。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：方法范围内仍有 1 处 `.what()`，一般数据库异常捕获仍绑定异常对象，固定完整摘要缺失；三条查询、关键 SQL、五项响应字段、成功日志和公开错误断言均通过。

实现后定向源码合同 1/1、全部 Admin GoogleTest 76/76、OpenSpec 24/24 和完整后端构建通过。真实管理流全部 25 个断言组通过、0 失败，系统概览返回真实用户/文件/活动分享聚合值，后续系统状态、配额和鉴权场景继续通过。标准完整 CTest 共 1521 项：1514 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 550.83 秒。源码审计确认 `GetOverviewStats()` 范围内固定数据库摘要 1 处、`.what()` 为 0；`AdminService.cpp` 其余 6 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.191 AdminService 系统状态诊断脱敏记录（2026-08-04）

`GetSystemStatus()` 的数据库、Redis 和磁盘探测异常只允许分别记录固定完整摘要 `admin.stats.system Database check failed`、`admin.stats.system Redis check failed` 和 `admin.stats.system disk space check failed`；message 不得追加异常正文、SQL、路径、连接信息、凭据或 token。`DrogonDbException`、`RedisException`、Redis 探测的其他 `std::exception` 与 `filesystem_error` 均必须匿名捕获。

各探测必须保持独立降级：数据库失败只置 `db_connected=false`，Redis 客户端缺失或两类异常只置 `redis_connected=false`，磁盘异常只将 total/used/free 置零；其他探测、uptime、成功日志、成功响应、鉴权和类型化请求上下文不得改变。验证必须新增源码合同锁定四个匿名捕获、三个固定摘要、探测调用、成功/降级字段赋值和 uptime，并执行 DTO/全部 Admin GoogleTest、OpenSpec、完整构建、真实管理员系统状态流及标准完整 CTest。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

旧实现上的定向源码合同按预期为 0/1，共 8 个失败断言：方法范围内精确检出 4 处 `.what()`，四类异常捕获均绑定异常对象，数据库/Redis/磁盘固定完整摘要均缺失；三个探测调用、成功与降级字段赋值、uptime 和成功日志断言均通过。

实现后定向源码合同 1/1、全部 Admin GoogleTest 77/77、`SystemStatusResponse` DTO 4/4、OpenSpec 24/24 和完整后端构建通过。真实管理流全部 25 个断言组通过、0 失败，系统状态返回 `db_connected=true`、`redis_connected=true` 和 uptime，后续缺失用户、配额与鉴权场景继续通过。标准完整 CTest 共 1522 项：1515 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 555.52 秒。源码审计确认 `GetSystemStatus()` 范围内四类异常均匿名捕获、`.what()` 为 0，数据库/Redis/磁盘固定摘要分别为 1/2/1 处；`AdminService.cpp` 其余 2 处 `.what()` 留待后续独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.192 AdminService 管理日志列表诊断脱敏记录（2026-08-04）

`GetAdminLogs()` 的计数或分页查询遇到数据库异常时，只允许记录固定完整摘要 `Admin list logs database error`；message 不得追加筛选值、分页值、异常正文、SQL、连接信息、审计详情、凭据或 token。异常必须匿名捕获，并继续返回既有 `InternalError` 与 `Failed to list operation logs`。

action/日期范围/target type/target name 五类可选筛选、计数与倒序分页两条参数化查询、64 位 LIMIT/OFFSET 绑定、总页数、可空字段映射、成功日志、成功响应、鉴权和类型化请求上下文不得改变。验证必须新增源码合同锁定匿名捕获、固定诊断、公开错误、筛选 SQL、两次查询、分页与字段映射，并执行 DTO/全部 Admin GoogleTest、OpenSpec、完整构建、真实管理员日志筛选流及标准完整 CTest。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：方法范围内仍有 1 处 `.what()`，数据库异常捕获仍绑定异常对象，固定完整摘要缺失；五类筛选、两条查询、64 位分页、四项分页响应、九项日志字段映射、成功日志和公开错误断言均通过。

实现后定向源码合同 1/1、全部 Admin GoogleTest 78/78、管理日志 DTO 9/9、OpenSpec 24/24 和完整后端构建通过。真实管理流全部 25 个断言组通过、0 失败，可用空间变更审计继续通过管理日志筛选读取，后续缺失用户、参数校验和鉴权场景继续通过。标准完整 CTest 共 1523 项：1516 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 550.89 秒。源码审计确认 `GetAdminLogs()` 范围内数据库异常匿名捕获与固定摘要各 1 处、`.what()` 为 0；`AdminService.cpp` 只剩 `LogOperation()` 的 1 处 `.what()`，留待独立批次。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

### 15.193 AdminService 审计写入诊断脱敏记录（2026-08-04）

`LogOperation()` 写入审计失败时，只允许记录固定完整摘要 `Failed to log operation`；message 不得追加 action、操作人/目标 ID、目标类型/名称、details、异常正文、SQL、连接信息、凭据或 token。数据库异常必须匿名捕获，审计持久化继续 fail-open，不重试、不抛出且不得改变调用方业务结果或公开响应。

写入前的 request/operation JSON 关联字段补充、单次参数化 `operation_logs` INSERT、operator/action/target/details 参数顺序、details JSON 序列化、`system` IP 占位和成功 DEBUG 日志不得改变。验证必须新增源码合同锁定匿名捕获、固定诊断、关联详情、INSERT、无重试/无传播和成功日志，并执行全部 Admin GoogleTest、OpenSpec、完整构建、真实管理员成功审计流及标准完整 CTest；完成后 `AdminService.cpp` 的 `.what()` 应收口为 0。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

旧实现上的定向源码合同按预期为 0/1，共 3 个失败断言：方法范围内仍有 1 处 `.what()`，数据库异常捕获仍绑定异常对象，固定完整摘要缺失；关联详情、单次 INSERT、参数顺序、JSON 序列化、成功日志和无重试/无传播断言均通过。

实现后定向源码合同 1/1、全部 Admin GoogleTest 79/79、OpenSpec 24/24 和完整后端构建通过。真实管理流全部 25 个断言组通过、0 失败，可用空间变更审计继续通过管理日志筛选读取；标准完整 CTest 共 1524 项：1517 项通过、PgBouncer/Prometheus、3 项 S3 与 2 项分布式目标环境门控共 7 项跳过、0 失败，总耗时 551.04 秒。源码审计确认 `LogOperation()` 的数据库异常匿名捕获与固定摘要各 1 处、无重试/无传播，`AdminService.cpp` 的 `.what()` 已收口为 0。Phase 10 与最终 Definition of Done 在其余迁移、兼容退役和目标环境门禁完成前继续保持未勾选。

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
