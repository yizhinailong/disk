# io_uring 可行性分析报告

## 1. Trantor/Drogon 现状

### 1.1 事件循环架构

Trantor 是 Drogon 的底层网络库，采用经典的 Reactor 模式。核心组件包括：

- **EventLoop**：每个线程拥有一个 EventLoop 实例，通过 `std::unique_ptr<Poller> poller_` 持有一个多路复用器。主循环调用 `poller_->poll()` 阻塞等待就绪事件，然后依次回调活跃 Channel。
- **Poller 抽象层**：`Poller` 是多路复用器的基类，Trantor 提供了三个实现：
  - `EpollPoller`（Linux）：基于 `epoll_create1` / `epoll_wait` / `epoll_ctl`
  - `KQueue`（macOS/FreeBSD）：基于 kqueue
  - `PollPoller`（通用回退）：基于 poll
- **Channel**：封装文件描述符及其关注的事件（EPOLLIN/EPOLLOUT 等），每个 fd 对应一个 Channel，由 Poller 管理。
- **EventLoopThreadPool**：创建 N 个 IO 线程，每个线程运行独立的 EventLoop。新连接通过 round-robin 分配到各线程。

在 Linux 上，`EpollPoller::poll()` 直接调用 `epoll_wait()`，超时后遍历就绪事件列表，将事件映射到 Channel 并填充到 `activeChannels_` 列表。`EventLoop::loop()` 随后遍历活跃 Channel，执行回调。这是典型的 epoll 边沿触发或水平触发模型。

### 1.2 网络数据收发

Trantor 的 `TcpConnectionImpl` 在 Channel 上注册读写事件。当连接可写时，从发送缓冲区链表中取数据，调用 `write()` 系统调用发送。对于文件传输，Trantor 提供了 `FileBufferNode`（Unix 实现），使用 `open()` + `read()` 将文件内容读入 16KB 的 `MsgBuffer`，再由连接写出。

这意味着 Trantor 的文件发送路径是：`read(fd, buffer)` → 用户态缓冲区 → `write(socket, buffer)`，即传统的 read/write 双拷贝模型。Drogon 在其上封装了 `HttpResponse::newFileResponse()`，对小文件走内存映射，对大于约 200KB 的文件使用 Linux `sendfile()` 系统调用实现零拷贝。

### 1.3 Disk 项目的 I/O 模型

我们的项目在 Trantor/Drogon 之上又增加了一层文件 I/O 抽象：

- **LocalFileStorage**：通过 `ConcurrentTaskQueue`（线程池，默认 4 线程，上限 8 线程）执行所有阻塞文件系统操作。上传分片写入（`ofstream::write`）、分片合并组装（顺序读取各分片 + 哈希计算 + 写入组装文件）、文件移动/删除、路径检查等全部卸载到独立线程池。
- **下载响应**：`DownloadResponder` 区分两条路径：
  - **Path A（>= 256KB）**：使用 `drogon::HttpResponse::newFileResponse()`，由 Drogon 内部走 `sendfile()` 零拷贝。
  - **Path B（< 256KB）**：使用 `drogon::HttpResponse::newStreamResponse()`，通过回调函数 `ifstream::read` 读取文件内容到用户态缓冲区。
- **分片合并（Assembly）**：独立的 `m_assembly_worker_queue`（1-4 线程），通过 `AssemblyWorkerPool` 控制并发和同 `upload_id` 单飞。

总体 I/O 架构：事件循环只处理网络 IO（epoll），文件 IO 全部通过线程池完成，两者通过协程（`co_await RunBlockingFilesystemTask`）桥接。

---

## 2. io_uring 概述

### 2.1 基本原理

io_uring 是 Linux 5.1（2019 年）引入的异步 I/O 接口，由 Jens Axboe 设计。核心机制是两个共享内存环形缓冲区：

- **提交队列（SQ）**：应用程序将 I/O 请求（SQE）写入 SQ tail，内核从 SQ head 读取并执行。
- **完成队列（CQ）**：内核将完成结果（CQE）写入 CQ tail，应用程序从 CQ head 读取结果。

与传统 epoll 的"就绪模型"不同，io_uring 是"完成模型"：应用程序提交操作后等待完成结果，无需在就绪后再发起系统调用。

### 2.2 关键特性

| 特性 | 内核版本 | 说明 |
|------|----------|------|
| 基本读写操作 | 5.1+ | IORING_OP_READ, IORING_OP_WRITE |
| 文件 I/O | 5.1+ | 支持文件描述符上的异步读写 |
| 网络操作 | 5.1+ | accept, send, recv, connect |
| 快速轮询（IOSQE_ASYNC） | 5.6+ | 内核自动轮询 SQ，减少系统调用 |
| 固定文件/缓冲区 | 5.1+（5.6+ 完善） | 注册文件描述符和缓冲区，减少内核开销 |
| 多射接收 | 5.19+ | `IORING_OP_RECVMULTISHOT` 持续接收 |
| 协议级特性 | 5.6+ | sendmsg、recvmsg 等 |
| 取消操作 | 5.1+ | `IORING_OP_ASYNC_CANCEL` |
| 超时和链接 | 5.1+ | 链式请求、超时控制 |
| sendfile 等价 | 5.1+ | 通过链接 read/write SQE 实现，5.6+ 有更优方案 |

### 2.3 性能优势

io_uring 相对 epoll 的性能优势主要来自三个方面：

1. **系统调用批量化**：epoll 模型下每次操作至少一次系统调用（epoll_wait + 各操作的 read/write），io_uring 可以一次 `io_uring_enter()` 提交多个请求，分摊上下文切换开销。
2. **零拷贝潜力**：通过固定缓冲区注册（`IORING_REGISTER_BUFFERS`）和固定文件注册（`IORING_REGISTER_FILES`），减少内核与用户空间的数据拷贝。
3. **无锁共享内存**：SQ/CQ 使用无锁环形缓冲区，内核和应用直接通过共享内存通信，避免了系统调用的进入/退出开销。

公开基准测试数据：

- echo server 场景（io_uring vs epoll）：高并发下 io_uring 吞吐量约高出 25-40%，p99 延迟降低约 20-30%。frevib 的 echo server 基准在 300 连接下 io_uring 达到 216K req/s vs epoll 的 145K req/s（约 49% 提升）。
- HTTP 服务器场景：ryanseipp 的测试显示 io_uring 实现吞吐量约 198K req/s vs epoll 的 160K req/s（约 24% 提升）。
- 文件 I/O 场景：io_uring 的优势在高并发随机读写时最明显，因为批量化提交的效果更显著。

### 2.4 内核版本要求

| 场景 | 最低版本 | 推荐版本 |
|------|----------|----------|
| 基本文件和网络 I/O | 5.1 | 5.6+ |
| IOSQE_ASYNC（快速轮询） | 5.6 | 5.10+ |
| 稳定生产使用 | 5.10 LTS | 5.15 LTS 或 6.1+ LTS |
| 完整特性（multishot 等） | 5.19+ | 6.1+ |

Ubuntu 22.04 LTS 默认内核为 5.15，满足生产使用要求。Ubuntu 24.04 LTS 默认内核为 6.5，可使用全部特性。

---

## 3. Trantor io_uring 支持现状

### 3.1 代码现状

对 Trantor 仓库（`an-tao/trantor`）的分析表明：

- **Poller 层**：Trantor 的多路复用抽象只有 `EpollPoller`（Linux）、`KQueue`（macOS）和 `PollPoller`（通用），没有 `IoUringPoller` 或任何 io_uring 相关代码。
- **EventLoop 层**：`EventLoop` 直接持有 `Poller`，唤醒机制使用 `eventfd`（Linux），没有 io_uring 相关逻辑。
- **BufferNode 层**：`FileBufferNode`（Unix）使用 `open()` + `read()` 读取文件，没有使用 io_uring 的异步文件读取。
- **CMake/依赖**：`vcpkg.json` 和 CMakeLists 中没有 `liburing` 依赖。

### 3.2 上游讨论

Drogon 仓库存在一个相关的 Issue：

- **Issue #1074**（2021-11-08）："support for IO_uring"
  - 请求者询问是否可以集成 io_uring 以获得更高性能。
  - 维护者回复提到：io_uring 模型与数据库驱动不兼容，当前驱动设计为与 epoll/kqueue 配合工作。
  - 该 Issue 被标记为 `enhancement`，截至 2026 年 5 月仍处于 Open 状态，没有任何关联的 PR。

- **无相关 PR**：在 Drogon 和 Trantor 的 PR 历史中，没有任何 io_uring 相关的实现或实验性代码。

### 3.3 结论

**Trantor/Drogon 目前不支持 io_uring，且上游没有积极的开发计划。** 维护者明确指出数据库驱动的兼容性是主要障碍，这意味着即使社区希望支持，也需要先解决数据库层的异步模型适配问题。

---

## 4. 可行性评估

### 4.1 需要变更的范围

要使 Trantor/Drogon 支持 io_uring，需要改动以下层次：

#### 4.1.1 Poller 层（核心）

需要新增 `IoUringPoller` 类，实现 `Poller` 接口：

```
Poller（基类）
├── EpollPoller（现有，Linux）
├── KQueue（现有，macOS）
├── PollPoller（现有，回退）
└── IoUringPoller（新增，Linux）
```

`IoUringPoller` 需要实现：
- `poll()`：调用 `io_uring_wait_cqe_nr()` 等待完成事件，将 CQE 映射到 Channel。
- `updateChannel()`：管理 io_uring 对文件描述符的注册/注销。
- `removeChannel()`：取消已提交的 io_uring 请求。

工作量估计：**500-800 行代码**，涉及 `liburing` 集成、CQE 到 Channel 的映射、错误处理等。

#### 4.1.2 EventLoop 层

EventLoop 的唤醒机制（当前使用 `eventfd`）需要适配 io_uring 的事件源。可以通过 io_uring 的 `IORING_OP_READ` 监听 eventfd，或使用 io_uring 的原生超时机制。

工作量估计：**100-200 行修改**。

#### 4.1.3 BufferNode 层

`FileBufferNode` 可以改为使用 `IORING_OP_READ` 进行异步文件读取，避免当前的同步 `read()` 调用。不过由于当前文件读取已经在独立线程池中执行，这里的收益有限。

#### 4.1.4 构建系统

需要：
- 在 vcpkg.json 中添加 `liburing` 依赖
- 在 CMakeLists.txt 中添加条件编译（`TRANTOR_USE_IO_URING`）
- 处理 liburing 不可用时的回退

工作量估计：**50-100 行修改**。

#### 4.1.5 数据库驱动适配

这是最大的障碍。Drogon 的 PostgreSQL 客户端使用 libpq，后者是基于 socket 的同步/异步接口。要让数据库操作走 io_uring，需要：
- 将 libpq 的 socket fd 注册到 io_uring
- 使用 `IORING_OP_READ`/`IORING_OP_WRITE` 代替 epoll 监听
- 确保 libpq 的非阻塞模式与 io_uring 的完成模型兼容

这涉及对 Drogon ORM 层和数据库驱动层的深度改造。

### 4.2 替代方案：仅文件 I/O 使用 io_uring

考虑到改动 Trantor 网络层的复杂性，一个更现实的方案是**仅在文件 I/O 层引入 io_uring**，不改动 Trantor 的事件循环：

- 在 `LocalFileStorage` 中使用 liburing 的异步文件读写，替代当前通过线程池执行的同步 `ofstream::write` / `ifstream::read`。
- 使用独立的 io_uring 实例（每个 IO 线程一个），将文件操作提交到 io_uring 的 SQ。
- 通过 `io_uring_wait_cqe()` 获取完成结果，然后回调到协程。

这种方案的改动范围小得多，但收益也有限，因为：

1. 当前文件 I/O 已经在独立线程池中，不阻塞事件循环。
2. io_uring 的主要优势在于减少系统调用和上下文切换次数，而我们的线程池已经将文件 I/O 与网络 I/O 分离。
3. 批量化提交的收益在我们的工作负载下不明显（每次上传只写一个分片）。

### 4.3 风险评估

| 风险 | 级别 | 说明 |
|------|------|------|
| 上游不支持 | 高 | Trantor/Drogon 没有 io_uring 支持，需要自行维护 fork 版本 |
| 数据库驱动兼容性 | 高 | libpq 与 io_uring 的集成需要大量改造 |
| 内核版本要求 | 中 | 生产环境需要 5.10+ 内核，限制了部署灵活性 |
| 调试复杂度 | 中 | io_uring 的异步模型比 epoll 更难调试 |
| 跨平台影响 | 中 | io_uring 仅限 Linux，macOS/Windows 仍需回退到 epoll/kqueue |
| 稳定性风险 | 低-中 | io_uring 在 5.10+ 内核上已经相当稳定，但仍有边界情况 |

---

## 5. 收益分析

### 5.1 网络层（epoll → io_uring）

#### 5.1.1 理论收益

io_uring 在高并发网络场景下相对 epoll 的主要优势是：
- 系统调用次数减少：一次 `io_uring_enter()` 可以提交多个操作，而 epoll 需要为每个就绪事件单独调用 read/write。
- p99 延迟降低约 20-30%。
- 吞吐量提升约 20-40%。

#### 5.1.2 实际收益评估

对我们的项目而言，网络层的 io_uring 收益有限：

1. **Drogon 已经足够快**：Drogon 在 TechEmpower 基准测试中长期位居 C++ Web 框架前列，epoll 性能已经很高。我们的瓶颈不在网络层。
2. **连接数有限**：网盘系统的并发连接数通常在千级，不是百万级。在这个规模下，epoll 的性能已经足够。
3. **HTTP 协议开销**：HTTP 解析、JSON 处理、数据库查询的耗时远大于网络 I/O 的系统调用开销。
4. **TLS 开销**：如果启用 HTTPS，TLS 握手和数据加解密的 CPU 开销远大于 epoll vs io_uring 的差异。

**预估收益：网络层 io_uring 改造对我们工作负载的性能提升 < 5%。**

### 5.2 文件 I/O 层

#### 5.2.1 下载路径

下载已通过两条路径优化：

- **大文件（>= 256KB）**：使用 `sendfile()` 零拷贝，内核直接从文件描述符传输到 socket，不经过用户态。这是当前 Linux 上最高效的文件传输方式，io_uring 无法提供更好的方案。
- **小文件（< 256KB）**：使用 `ifstream::read` + 流式回调。io_uring 的异步文件读取可以替代这里的同步读取，但小文件的 I/O 时间本来就短，收益微乎其微。

**预估收益：下载路径几乎无收益。**

#### 5.2.2 上传路径

上传分片写入的流程是：
1. 接收 HTTP 请求体（已通过 epoll 异步完成）
2. 通过线程池执行 `ofstream::write` 将分片写入临时文件
3. 分片合并时，通过线程池顺序读取各分片、计算哈希、写入组装文件

io_uring 可以改进的地方：
- 将 `ofstream::write` 替换为 `IORING_OP_WRITE`，减少线程池的上下文切换开销。
- 在合并阶段，可以并发提交多个分片的读取请求（当前是顺序读取），然后并行计算哈希。

但是：
- 上传分片通常是 5MB，单次 `write()` 调用的耗时在 SSD 上约 1-5ms，线程池的上下文切换开销约 1-2μs，占比极小。
- 合并阶段的顺序读取是为了保证 MD5/SHA256 的哈希顺序，并发读取需要额外协调。
- 线程池已经提供了充足的并发能力（4-8 线程），瓶颈通常在磁盘带宽，不在 CPU 或系统调用。

**预估收益：上传路径 io_uring 改造的性能提升 < 3%。**

### 5.3 其他收益

- **系统调用减少**：io_uring 的批量化提交可以减少系统调用次数，但在我们的工作负载下（非高频短请求），这个收益不明显。
- **CPU 利用率**：io_uring 可以减少上下文切换导致的 CPU 缓存失效，但我们的线程池已经将文件 I/O 与网络 I/O 分离，CPU 缓存影响有限。
- **延迟一致性**：io_uring 的完成模型可以提供更稳定的延迟，但我们的服务延迟瓶颈在数据库（PostgreSQL 查询）和网络（Redis 通信），不在文件 I/O。

### 5.4 综合评估

| 场景 | 预估性能提升 | 改造复杂度 | 投入产出比 |
|------|-------------|-----------|-----------|
| 网络层全面改造 | < 5% | 极高（需 fork Trantor） | 极低 |
| 仅文件 I/O 改造 | < 3% | 中 | 低 |
| 保持现状 | 0% | 无 | N/A |

---

## 6. 建议

### 6.1 决策：No-Go

**建议在当前阶段不引入 io_uring。**

### 6.2 理由

1. **投入产出比极低**：全面改造需要 fork 并维护 Trantor 的 io_uring 版本，预估工作量数百小时，而性能提升不足 5%。仅文件 I/O 改造的收益更小（< 3%），且增加了代码复杂度和外部依赖。

2. **上游不支持**：Drogon/Trantor 没有 io_uring 支持计划。Issue #1074 自 2021 年提出至今未有任何进展，维护者明确指出了数据库驱动的兼容性问题。自行 fork 维护意味着长期的维护负担。

3. **现有架构已优化到位**：
   - 下载路径已使用 `sendfile()` 零拷贝，这是当前 Linux 上最高效的文件传输方式。
   - 文件 I/O 已通过线程池与网络 I/O 分离，不阻塞事件循环。
   - 协程模型已经实现了高效的异步编程。

4. **真正的瓶颈不在此**：压测和线上数据分析表明，我们的性能瓶颈主要在：
   - PostgreSQL 查询（复杂联表、事务）
   - Redis 网络往返
   - 文件系统的磁盘带宽（非系统调用开销）
   - HTTP 解析和 JSON 序列化

5. **部署灵活性降低**：io_uring 要求 Linux 5.10+ 内核，且不同内核版本的行为和性能存在差异。引入 io_uring 会限制部署环境的选择。

### 6.3 替代优化方向

在放弃 io_uring 的前提下，建议关注以下更有价值的优化方向：

1. **数据库层优化**：索引优化、查询重写、连接池调优、读写分离。
2. **缓存策略优化**：Redis 缓存预热、热点数据缓存、减少不必要的数据库查询。
3. **批量操作优化**：合并多个数据库操作为批量操作，减少网络往返。
4. **文件 I/O 优化**：调整合并缓冲区大小、优化分片大小、使用 `O_DIRECT` 绕过页缓存（对大文件写入可能有效）。
5. **网络层优化**：启用 HTTP/2、调整 TCP 参数、使用 `TCP_CORK`/`TCP_NODELAY`。

### 6.4 重新评估时机

建议在以下条件满足时重新评估 io_uring：

- Drogon/Trantor 上游正式支持 io_uring（关注 Issue #1074 的进展）。
- 工作负载发生变化（例如需要支持万级并发连接或高频小文件操作）。
- Linux 内核 6.x 成为主流部署环境，io_uring 特性更加成熟。
- 出现基于 io_uring 的高性能 C++ Web 框架，可以替代 Drogon。
