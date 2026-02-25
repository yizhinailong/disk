# 文件列表页面设计

## 概述

文件列表是 Disk 客户端的核心组件，展示当前目录下的文件和文件夹，支持网格视图和列表视图两种显示模式。

视觉样式：默认使用目标框架的默认样式与系统配色；本文档仅约束布局与交互，不约束具体配色、字体或深浅色切换。
---

## Qt/QML 设计

### 网格视图

```
┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │          │  │          │  │          │  │          │           │
│  │   📁     │  │   📄     │  │   📷     │  │   📁     │           │
│  │          │  │          │  │          │  │          │           │
│  │ 项目文档  │  │ 年度报告  │  │ 团队合影  │  │ 会议记录  │           │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘           │
│                                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │          │  │          │  │          │  │          │           │
│  │   🎬     │  │   📦     │  │   📄     │  │   🎵     │           │
│  │          │  │          │  │          │  │          │           │
│  │ 产品演示  │  │ 项目归档  │  │ 需求文档  │  │ 背景音乐  │           │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**卡片尺寸**: 100px x 100px（可调整）  
**间距**: 16px  
**图标大小**: 48px

### 列表视图

```
┌─────────────────────────────────────────────────────────────────────┐
│  名称                      大小         修改时间         类型       │
├─────────────────────────────────────────────────────────────────────┤
│  📁 项目文档               -           2026-02-15      文件夹       │
│  📄 年度报告.docx          1.2 MB      2026-02-15      文档         │
│  📷 团队合影.jpg           2.3 MB      2026-02-10      图片         │
│  🎬 产品演示.mp4           45.6 MB     2026-02-08      视频         │
│  📦 项目归档.zip           156 MB      2026-02-05      压缩包       │
│  📄 需求文档.pdf           890 KB      2026-02-03      PDF          │
│  🎵 背景音乐.mp3           3.8 MB      2026-02-01      音频         │
└─────────────────────────────────────────────────────────────────────┘
```

**行高**: 40px  
**列宽**: 名称(自适应)、大小(80px)、修改时间(120px)、类型(80px)

### 列表字段

| 字段 | 类型 | 网格视图 | 列表视图 | 说明 |
|------|------|----------|----------|------|
| icon | image | ✓ | ✓ | 文件类型图标/缩略图 |
| name | string | ✓ | ✓ | 文件名 |
| size | string | - | ✓ | 文件大小 |
| modified | datetime | - | ✓ | 修改时间 |
| type | string | - | ✓ | 文件类型 |

### 交互设计

- **双击文件夹**: 进入文件夹
- **双击文件**: 下载/预览
- **右键**: 显示上下文菜单
- **拖拽**: 文件移动
- **多选**: Ctrl+点击、Shift+点击

### 空目录显示

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│                    📭 此文件夹为空                               │
│                                                                 │
│                    按 [u] 上传文件                               │
│                    按 [n] 创建文件夹                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Qt Widget 设计

### 网格视图 (QListWidget + IconMode)

```cpp
QWidget* MainWindow::createGridViewPage() {
    QListWidget* gridView = new QListWidget();
    gridView->setViewMode(QListView::IconMode);
    gridView->setIconSize(QSize(48, 48));
    gridView->setGridSize(QSize(100, 100));
    gridView->setSpacing(16);
    gridView->setResizeMode(QListView::Adjust);
    gridView->setMovement(QListView::Static);
    
    // 添加文件项
    QListWidgetItem* item = new QListWidgetItem(
        QIcon(":/icons/folder.svg"), "项目文档");
    gridView->addItem(item);
    
    return gridView;
}
```


### 列表视图 (QTableWidget)

```cpp
QWidget* MainWindow::createListViewPage() {
    QTableWidget* tableView = new QTableWidget();
    tableView->setColumnCount(4);
    tableView->setHorizontalHeaderLabels({
        tr("名称"), tr("大小"), tr("修改时间"), tr("类型")
    });
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableView->setAlternatingRowColors(true);
    tableView->verticalHeader()->setVisible(false);
    
    // 设置列宽
    tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableView->setColumnWidth(1, 100);
    tableView->setColumnWidth(2, 150);
    tableView->setColumnWidth(3, 100);
    
    return tableView;
}
```


### 视图切换实现

```cpp
class FileListView : public QWidget {
    Q_OBJECT
public:
    enum ViewMode { Grid, List };
    
    explicit FileListView(QWidget* parent = nullptr);
    void SetViewMode(ViewMode mode);
    void SetModel(FileListModel* model);
    
signals:
    void ItemDoubleClicked(const FileItem& item);
    void SelectionChanged(const QList<FileItem>& items);
    void ContextMenuRequested(const QPoint& pos, const FileItem& item);
    
private:
    QStackedWidget* m_stackedWidget;
    QListView* m_gridView;      // 网格视图
    QTableView* m_tableView;    // 列表视图
    FileListModel* m_model;
};
```

---

## TUI 设计

### 列表项格式

```
<图标> <名称> ..................................... <大小>
```

**列布局**:

| 列 | 宽度 | 对齐 | 说明 |
|----|------|------|------|
| 图标 | 2字符 | 左对齐 | 文件类型图标 |
| 名称 | 自适应 | 左对齐 | 文件/文件夹名称 |
| 点填充 | 自适应 | - | 用 `.` 填充空白 |
| 大小 | 10字符 | 右对齐 | 文件大小（带单位） |

### 选中状态

- 背景: 选中高亮色
- 前景: 白色
- 左侧添加 `▶` 指示器

**示例**:
```
  📁 2024项目
▶ 📄 年度报告.docx ..................................... 1.2 MB   ← 选中项
  📷 团队合影.jpg ..................................... 2.3 MB
```

### 完整界面示例

```
┌───────────────────────────────────────────────────────────────┐
│ 📁 /文档/工作                                         [? 帮助] │
├───────────────────────────────────────────────────────────────┤
│   名称                    大小         创建时间       操作    │
├───────────────────────────────────────────────────────────────┤
│ 📁 项目文档               12 项        2026-01-10     >       │
│ 📁 会议记录               8 项         2026-01-08     >       │
│ 📄 年度报告.pdf           2.3 MB       2026-01-12     [空格]  │
│ 📄 工作计划.docx          156 KB       2026-01-11     [空格]  │
│ 📄 数据分析.xlsx          890 KB       2026-01-09     [空格]  │
├───────────────────────────────────────────────────────────────┤
│ 第 1/3 页  共 15 项  [PgUp/PgDn 翻页]  [q 返回上级]           │
└───────────────────────────────────────────────────────────────┘
```

### 分页策略

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `page_size` | 自动计算 | 终端高度 - 固定行数 |
| `visible_items` | `page_size` | 当前页显示项数 |
| `scroll_threshold` | 3 | 距边缘 N 行时触发滚动 |

**滚动行为**:
- 选中到列表底部时，自动向下滚动一页
- 选中到列表顶部时，自动向上滚动一页
- 支持 `Ctrl+D` / `Ctrl+U` 半页滚动
- 支持 `Ctrl+F` / `Ctrl+B` 整页滚动

### 导航快捷键

| 快捷键 | 功能 | vim 等价 |
|--------|------|----------|
| `j` / `↓` | 列表项下移 | j |
| `k` / `↑` | 列表项上移 | k |
| `gg` | 跳到列表顶部 | gg |
| `G` | 跳到列表底部 | G |
| `{n}G` | 跳到第 n 项 | {n}G |
| `Enter` | 进入/确认 | - |
| `Esc` / `q` | 返回/取消 | - |
| `Backspace` | 返回上级 | - |

---

## 通用设计规范

### 文件图标

#### 文件夹图标

| 状态 | 图标 | 说明 |
|------|------|------|
| 普通 | 📁 | 默认文件夹 |
| 打开 | 📂 | 当前目录 |
| 收藏 | ⭐ | 收藏夹 |

#### 文件类型图标

| 类型 | 图标 | 扩展名 |
|------|------|--------|
| 文档 | 📄 | doc, docx, txt |
| 表格 | 📊 | xls, xlsx, csv |
| 演示 | 🎥️ | ppt, pptx |
| PDF | 📕 | pdf |
| 图片 | 🖼️ | jpg, png, gif |
| 视频 | 🎦 | mp4, avi, mkv |
| 音频 | 🎵 | mp3, wav, flac |
| 压缩包 | 📦 | zip, rar, 7z |
| 代码 | 💻 | cpp, py, js |
| 未知 | 📎 | 其他 |

### 文件大小格式化

| 大小范围 | 显示格式 | 示例 |
|----------|----------|------|
| < 1 KB | `xxx B` | `512 B` |
| < 1 MB | `xxx KB` | `1.5 KB` |
| < 1 GB | `xxx MB` | `2.3 MB` |
| >= 1 GB | `xxx GB` | `1.2 GB` |

### 业务规则

1. 文件和文件夹混合显示，通过图标区分
2. 文件夹显示在顶部（可配置）
3. 支持按名称/大小/修改时间/类型排序
4. 支持升序/降序切换
5. 空目录显示友好提示
6. 支持多选（Ctrl+点击、Shift+点击）

---

*最后更新: 2026-02-19*
