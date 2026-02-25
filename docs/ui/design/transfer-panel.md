# 传输面板设计

## 概述

传输面板用于显示和管理文件上传/下载任务，支持查看进度、暂停、继续和取消操作。

视觉样式：默认使用目标框架的默认样式与系统配色；本文档仅约束布局与交互，不约束具体配色、字体或深浅色切换。
---

## Qt/QML 设计

### 面板布局

**高度**: 0-200px（可展开/收起）

```
┌─────────────────────────────────────────────────────────────────────┐
│  传输列表 (3)                                           [▼ 收起]    │
├─────────────────────────────────────────────────────────────────────┤
│  📤 上传中                                                            │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 文档.pdf                    45%     1.2 MB/s     [⏸] [✕]    │   │
│  │ ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │   │
│  └─────────────────────────────────────────────────────────────┘   │
│  📥 下载中                                                            │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 项目.zip                   67%     2.1 MB/s     [⏸] [✕]    │   │
│  │ ██████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 传输项格式

```
┌─────────────────────────────────────────────────────────────────────┐
│ <图标> <文件名>                    <进度>%     <速度>     [⏸] [✕] │
│ <进度条>                                                            │
└─────────────────────────────────────────────────────────────────────┘
```

### 操作按钮

| 按钮 | 功能 |
|------|------|
| ⏸ | 暂停任务 |
| ▶ | 继续任务 |
| ✕ | 取消任务 |

### 状态颜色

| 状态 | 说明 |
|------|------|
| 进行中 | 正在传输 |
| 已暂停 | 用户暂停 |
| 已完成 | 传输完成 |
| 失败 | 传输失败 |
| 等待中 | 等待开始 |

---

## Qt Widget 设计

### TransferPanel 实现

```cpp
class TransferPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransferPanel(QWidget* parent = nullptr);
    
public slots:
    void OnUploadTaskAdded(UploadTask* task);
    void OnDownloadTaskAdded(DownloadTask* task);
    
private slots:
    void OnPauseSelected();
    void OnResumeSelected();
    void OnCancelSelected();
    void OnClearCompleted();
    void OnPauseAll();
    void OnResumeAll();
    
private:
    void SetupUi();
    void UpdateToolbarState();
    
    QTabWidget* m_tabWidget;
    QTreeView* m_uploadView;
    QTreeView* m_downloadView;
    TransferModel* m_uploadModel;
    TransferModel* m_downloadModel;
    
    QPushButton* m_pauseBtn;
    QPushButton* m_resumeBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_clearBtn;
};
```

### TransferItemWidget 实现

```cpp
class TransferItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit TransferItemWidget(const TransferInfo& info, QWidget* parent = nullptr);
    void UpdateProgress(int percent);
    
private:
    QLabel* m_nameLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QPushButton* m_pauseBtn;
    QPushButton* m_cancelBtn;
};
```

### TransferModel 数据模型

```cpp
class TransferModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Size, Progress, Speed, State, Action };
    
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    
    void AddTask(TransferTask* task);
    void RemoveTask(const QString& taskId);
    void UpdateTaskProgress(const QString& taskId, int progress, qint64 speed);
};
```


### 面板展开动画

```cpp
void TransferPanel::togglePanel() {
    QPropertyAnimation* animation = new QPropertyAnimation(this, "maximumHeight");
    animation->setDuration(200);
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    
    if (m_expanded) {
        animation->setStartValue(height());
        animation->setEndValue(40);  // 收起状态高度
    } else {
        animation->setStartValue(height());
        animation->setEndValue(200);  // 展开状态高度
    }
    
    m_expanded = !m_expanded;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
```

---

## TUI 设计

### 传输状态栏

**高度**: 0 ~ N 行（无传输时隐藏）

**显示条件**:
- 有文件正在上传/下载时显示
- 多个传输任务时分行显示
- 最多显示 3 个任务，超出显示 `+N 个任务...`

### 单个任务格式

```
<类型>: <文件名> <进度条> <百分比> │ <速度>
```

### 上传队列界面

```
┌───────────────────────────────────────────────────────────────┐
│                        上传队列                               │
├───────────────────────────────────────────────────────────────┤
│ 文件名              大小      进度      状态      速度        │
├───────────────────────────────────────────────────────────────┤
│ 报告.pdf           25 MB     45%      上传中    1.2 MB/s     │
│ ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│                                                               │
│ 数据.zip           100 MB    0%       等待中    -            │
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│                                                               │
│ 文档.docx          156 KB    100%     完成      -            │
│ ████████████████████████████████████████████████████████████ │
├───────────────────────────────────────────────────────────────┤
│ [p 暂停] [c 取消选中] [a 取消全部]                            │
└───────────────────────────────────────────────────────────────┘
```

### 下载队列界面

```
┌───────────────────────────────────────────────────────────────┐
│                        下载队列                               │
├───────────────────────────────────────────────────────────────┤
│ 文件名              大小      进度      状态      速度        │
├───────────────────────────────────────────────────────────────┤
│ 项目文档.pdf       25 MB     67%      下载中    2.1 MB/s     │
│ ██████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│                                                               │
│ 报告.docx          156 KB    100%     完成      -            │
│ ████████████████████████████████████████████████████████████ │
├───────────────────────────────────────────────────────────────┤
│ [p 暂停] [c 取消选中] [o 打开目录]                            │
└───────────────────────────────────────────────────────────────┘
```

### 传输状态栏（紧凑显示）

在主界面中显示的紧凑传输状态：

```
┌─────────────────────────────────────────────────────────────────────────┐
│  上传: 文档.pdf ██████████████████░░░░░░ 67% │ 速度: 2.1 MB/s           │
├─────────────────────────────────────────────────────────────────────────┤
│  [u]上传 [d]下载 [r]重命名 [m]移动 [c]复制 [x]删除 [n]新建 [?]帮助     │
└─────────────────────────────────────────────────────────────────────────┘
```

### 专用快捷键

| 快捷键 | 功能 |
|--------|------|
| `t` | 打开传输队列面板 |
| `p` | 暂停选中的任务 |
| `c` | 继续暂停的任务 |
| `x` | 取消选中的任务 |
| `l` | 清理已完成的任务 |
| `P` | 暂停所有任务 |
| `C` | 继续所有暂停任务 |

### 状态图标

| 状态 | 图标 | 说明 |
|------|------|------|
| 上传 | 📤 | 上传任务 |
| 下载 | 📥 | 下载任务 |
| 进行中 | - | 蓝色进度条 |
| 已暂停 | ⏸ | 黄色进度条 |
| 已完成 | ✓ | 绿色进度条 |
| 失败 | ✗ | 红色进度条 |

---

## 通用设计规范

### 传输队列管理功能

| 功能 | 说明 |
|------|------|
| 查看传输队列 | 显示所有传输任务 |
| 暂停任务 | 暂停选中的任务 |
| 继续任务 | 继续暂停的任务 |
| 取消任务 | 取消选中的任务 |
| 清理已完成 | 清理已完成的任务 |
| 全部暂停 | 暂停所有任务 |
| 全部继续 | 继续所有暂停任务 |

### 进度条规范

| 参数 | 值 |
|------|-----|
| 高度 | 4px |
| 已完成颜色 | Primary 色 |
| 未完成颜色 | Border 色 |
| 圆角 | 2px |
| TUI 长度 | 20-40 字符 |

### 文件大小格式化

| 大小范围 | 显示格式 | 示例 |
|----------|----------|------|
| < 1 KB | `xxx B` | `512 B` |
| < 1 MB | `xxx KB` | `1.5 KB` |
| < 1 GB | `xxx MB` | `2.3 MB` |
| >= 1 GB | `xxx GB` | `1.2 GB` |

### 速度显示

| 速度范围 | 显示格式 | 示例 |
|----------|----------|------|
| < 1 KB/s | `xxx B/s` | `512 B/s` |
| < 1 MB/s | `xxx KB/s` | `512 KB/s` |
| >= 1 MB/s | `xxx MB/s` | `2.1 MB/s` |

### 剩余时间计算

```
剩余时间 = (总大小 - 已传输) / 当前速度
```

**显示格式**:
- < 1 分钟: `约 xx 秒`
- < 1 小时: `约 xx 分钟`
- >= 1 小时: `约 xx 小时 xx 分钟`

### 业务规则

| 规则 | 说明 |
|------|------|
| 传输队列持久化 | 应用重启后恢复 |
| 传输状态实时更新 | 进度、速度、剩余时间 |
| 任务优先级调整 | 支持调整优先级 |
| 失败任务重试 | 支持手动重试 |

---

*最后更新: 2026-02-19*
