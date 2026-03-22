# 文件列表页面设计 (v2.0)

## 概述

文件列表是 Disk 客户端的核心功能页面，用于展示和管理用户的文件与文件夹。参考百度网盘文件管理界面，提供网格视图、列表视图两种展示模式，支持丰富的文件操作和交互体验。

**视觉风格**: 简洁卡片式布局 + 流畅操作体验 + 现代化视觉反馈

---

## 整体布局

```mermaid
flowchart TD
    subgraph MainContent["文件列表页面"]
        subgraph Header["标题栏"]
            Title["全部文件"]
        end
        
        subgraph Toolbar["工具栏"]
            Breadcrumb["面包屑: 全部文件 > 工作 > 项目"]
            ViewToggle["⊞ 网格 | ☰ 列表"]
            SortDropdown["排序 ▼"]
            MoreMenu["更多 ▼"]
        end
        
        subgraph FileList["文件列表区域"]
            subgraph ListHeader["列表头"]
                Col1["□ 文件名"]
                Col2["大小"]
                Col3["类型"]
                Col4["修改时间"]
            end
            
            subgraph Folders["文件夹"]
                F1["□ 📁 项目资料  -  文件夹  今天 10:23"]
                F2["□ 📁 会议记录  -  文件夹  昨天 16:45"]
                F3["□ 📁 归档文件  -  文件夹  2026-03-10"]
            end
            
            subgraph Files["文件"]
                Fi1["□ 📄 需求文档.docx  2.3 MB  Word  今天 09:15"]
                Fi2["□ 🎬 产品演示.mp4  156 MB  视频  昨天 14:30"]
                Fi3["□ 📊 数据统计.xlsx  890 KB  Excel  昨天 11:20"]
                Fi4["□ 📷 团队合影.jpg  5.6 MB  图片  2026-03-12"]
                Fi5["□ 📕 产品白皮书.pdf  12.5 MB  PDF  2026-03-08"]
                Fi6["□ 📦 项目资料.zip  45.2 MB  压缩包  2026-03-05"]
            end
            
            subgraph Pagination["分页"]
                Pages["1  2  3  ...  10  [>]"]
            end
        end
    end
```

---

## 视图模式

### 网格视图

```mermaid
flowchart TD
    subgraph GridView["网格视图"]
        subgraph Row1["第一行"]
            Card1["📁 项目资料"]
            Card2["📁 会议记录"]
            Card3["📁 归档文件"]
            Card4["📄 需求文档.docx"]
            Card5["🎬 产品演示.mp4"]
        end
        
        subgraph Row2["第二行"]
            Card6["📊 数据统计.xlsx"]
            Card7["📷 团队合影.jpg"]
            Card8["📕 产品白皮书.pdf"]
            Card9["📦 项目资料.zip"]
            Card10["💻 脚本工具.py"]
        end
    end
```

#### 网格卡片规格

| 属性 | 值 |
|------|------|
| 卡片尺寸 | 120 x 140px |
| 图标区域 | 120 x 96px |
| 图标尺寸 | 64px |
| 信息区域 | 120 x 44px |
| 圆角 | 12px |
| 间距 | 16px |
| 选中边框 | 2px Primary |

#### 卡片状态

```mermaid
flowchart LR
    subgraph Default["默认状态"]
        D1["📁 文件名\nShadow SM"]
    end
    
    subgraph Hover["悬停状态"]
        H1["📁 文件名\nShadow MD + 边框高亮"]
    end
    
    subgraph Selected["选中状态"]
        S1["📁 文件名\nPrimary Light 背景"]
    end
    
    Default --> Hover --> Selected
```

### 列表视图

```mermaid
flowchart TD
    subgraph ListView["列表视图"]
        Header["□ 文件名 | 大小 | 类型 | 修改时间 | 操作"]
        Row1["□ 📁 项目资料  -  文件夹  今天 10:23  ⋮"]
        Row2["□ 📁 会议记录  -  文件夹  昨天 16:45  ⋮"]
        Row3["□ 📄 需求文档.docx  2.3 MB  Word  今天 09:15  ⋮"]
        Row4["□ 🎬 产品演示.mp4  156 MB  视频  昨天 14:30  ⋮"]
        Row5["□ 📊 数据统计.xlsx  890 KB  Excel  昨天 11:20  ⋮"]
        Row6["□ 📷 团队合影.jpg  5.6 MB  图片  2026-03-12  ⋮"]
    end
```

#### 列表行规格

| 属性 | 值 |
|------|------|
| 行高 | 52px |
| 复选框 | 18px，左侧 16px |
| 图标 | 24px |
| 文件名 | 14px，加粗 |
| 其他列 | 14px，常规 |
| 悬停背景 | Hover (#F5F6F7) |
| 选中背景 | Primary Light (#E6F6FF) |

#### 列表列定义

| 列名 | 宽度 | 对齐 | 说明 |
|------|------|------|------|
| 复选框 | 48px | 居中 | 全选/单选 |
| 文件名 | 自适应 | 左对齐 | 图标 + 文件名 + 扩展名 |
| 大小 | 100px | 右对齐 | 文件大小 |
| 类型 | 100px | 左对齐 | 文件类型描述 |
| 修改时间 | 150px | 左对齐 | 最后修改时间 |
| 操作 | 48px | 居中 | 更多操作按钮 |

---

## 工具栏设计

### 面包屑导航

```
全部文件 > 工作 > 项目 > 文档
   ↑       ↑     ↑
 可点击  可点击  当前目录（高亮）
```

**交互**:
- 点击上级目录: 跳转到对应目录
- 目录过长: 显示省略号，hover 展开
- 当前目录: Text Primary，不可点击

### 视图切换

```mermaid
flowchart LR
    subgraph ViewToggle["视图切换"]
        Grid["⊞ 网格"]
        List["☰ 列表"]
    end
    Grid --- List
```

- 胶囊按钮组，高度 32px
- 选中: Primary 背景
- 快捷键: Ctrl+1 (网格), Ctrl+2 (列表)

### 排序选项

```mermaid
flowchart TD
    SortBtn["排序 ▼"]
    SortBtn --> Options
    
    subgraph Options["排序选项"]
        NameAsc["📄 名称 (A-Z)"]
        NameDesc["📄 名称 (Z-A)"]
        SizeAsc["📊 大小 (小→大)"]
        SizeDesc["📊 大小 (大→小)"]
        ModifiedNew["📅 修改时间 (新→旧)"]
        ModifiedOld["📅 修改时间 (旧→新)"]
        CreatedNew["📅 创建时间 (新→旧)"]
        CreatedOld["📅 创建时间 (旧→新)"]
    end
```

### 更多操作

```mermaid
flowchart TD
    MoreBtn["更多 ▼"]
    MoreBtn --> Menu
    
    subgraph Menu["更多菜单"]
        Display["👁 显示方式"]
        GridView["⊞ 网格视图"]
        ListView["☰ 列表视图"]
        Refresh["📋 刷新"]
        Settings["⚙ 文件夹设置"]
    end
    
    Display --> GridView
    Display --> ListView
```

---

## 批量操作栏

当选中文件/文件夹时，显示批量操作栏:

```mermaid
flowchart TD
    subgraph BatchOps["批量操作栏"]
        Selected["☑️ 已选择 5 个项目"]
        
        subgraph Actions["操作按钮"]
            Download["⬇ 下载"]
            Share["🔗 分享"]
            Move["📁 移动到..."]
            Copy["📋 复制到..."]
            Cut["✂ 剪切"]
            Delete["🗑️ 删除"]
        end
        
        Cancel["取消选择"]
    end
    
    Selected --> Actions
    Actions --> Cancel
```

### 批量操作按钮

| 按钮 | 功能 | 说明 |
|------|------|------|
| ⬇ 下载 | 批量下载 | 打包为 zip 下载 |
| 🔗 分享 | 批量分享 | 创建多文件分享链接 |
| 📁 移动到... | 移动文件 | 弹出文件夹选择对话框 |
| 📋 复制到... | 复制文件 | 弹出文件夹选择对话框 |
| ✂ 剪切 | 剪切文件 | 准备移动 |
| 🗑️ 删除 | 删除文件 | 移至回收站 |

### 批量操作栏动画

- 出现: 从底部滑入 200ms
- 消失: 向下滑出 150ms
- 背景: Primary Light (#E6F6FF)
- 阴影: Shadow MD

---

## 空目录设计

```mermaid
flowchart TD
    subgraph EmptyState["空文件夹状态"]
        Icon["📂"]
        Title["空文件夹"]
        Message["此文件夹还没有内容"]
        
        subgraph Actions["操作按钮"]
            Upload["⬆ 上传文件"]
            NewFolder["📁 新建文件夹"]
        end
    end
    
    Icon --> Title --> Message --> Actions
```

---

## 右键菜单

### 文件菜单

```mermaid
flowchart TD
    subgraph FileMenu["文件右键菜单"]
        Preview["👁 预览  ← 仅支持预览的文件类型"]
        Download["⬇ 下载"]
        Rename["✏ 重命名"]
        Copy1["📋 复制"]
        Cut["✂ 剪切"]
        Delete1["🗑️ 删除"]
        MoveTo["📁 移动到..."]
        CopyTo["📋 复制到..."]
        Share["🔗 分享"]
        Favorite["⭐ 收藏  ← 添加到收藏夹"]
        Properties["ℹ️ 属性  ← 查看文件详情"]
    end
```

### 文件夹菜单

```mermaid
flowchart TD
    subgraph FolderMenu["文件夹右键菜单"]
        Open["📂 打开"]
        Rename2["✏ 重命名"]
        Copy2["📋 复制"]
        Cut2["✂ 剪切"]
        Delete2["🗑️ 删除"]
        MoveTo2["📁 移动到..."]
        CopyTo2["📋 复制到..."]
        Share2["🔗 分享"]
        Favorite2["⭐ 收藏"]
        Properties2["ℹ️ 属性"]
    end
```

### 空白处菜单

```mermaid
flowchart TD
    subgraph BlankMenu["空白处右键菜单"]
        UploadFile["⬆ 上传文件"]
        NewFolder["📁 新建文件夹"]
        Paste["📋 粘贴  ← 有剪切/复制内容时可用"]
        ViewSub["👁 查看"]
        Refresh["📋 刷新"]
    end
    
    ViewSub --> GridView2["⊞ 网格"]
    ViewSub --> ListView2["☰ 列表"]
```

---

## 拖拽交互

### 文件拖拽上传

```mermaid
flowchart TD
    subgraph DragUpload["拖拽上传状态"]
        subgraph DropZone["拖放区域"]
            Icon["📤"]
            Title["拖拽文件到此处上传"]
            Hint["支持多文件、文件夹上传"]
        end
        Tip["拖拽文件到窗口任意位置，松开后自动上传"]
    end
    
    Icon --> Title --> Hint
    DropZone --> Tip
```

**拖拽状态**:
- 边框: 2px dashed Primary
- 背景: Primary Light 20% 透明度
- 图标: 上传图标放大动画

### 文件拖拽移动

```mermaid
flowchart LR
    subgraph Dragging["拖拽中"]
        DragIcon["📄<br/>3个项目<br/><small>拖拽时的缩略图</small>"]
    end
    
    subgraph DropTarget["拖拽到文件夹上"]
        FolderIcon["📁<br/>目标文件夹<br/><small>文件夹高亮</small>"]
    end
    
    Dragging -->|"移动到"| DropTarget
```

---

## 分页设计

```mermaid
flowchart LR
    subgraph Pagination["分页组件"]
        Info["显示 1-50 条，共 128 条"]
        Nav["<  1  2  3  ... 10  >"]
    end
```

### 分页组件

| 元素 | 说明 |
|------|------|
| 总数显示 | "显示 X-Y 条，共 Z 条" |
| 上一页 | < 按钮，首页禁用 |
| 页码 | 当前页 Primary 背景 |
| 省略 | ... 表示省略页 |
| 下一页 | > 按钮，末页禁用 |

### 分页规格

- 每页默认: 50 条
- 可选: 25 / 50 / 100 / 200
- 快速跳转: 输入页码跳转

---

## 文件图标设计

### 文件夹图标

| 状态 | 图标 | 尺寸 | 说明 |
|------|------|------|------|
| 默认 | 📁 | 64px | 蓝色文件夹 |
| 打开 | 📂 | 64px | 当前所在文件夹 |
| 加密 | 🔒 | 20px 角标 | 加密文件夹标记 |

### 文件类型图标

| 类型 | 图标 | 背景色 | 扩展名 |
|------|------|--------|--------|
| 图片 | 🖼️ | #E6F6FF (蓝) | jpg, png, gif, bmp, webp |
| 视频 | 🎬 | #FFF0F0 (红) | mp4, avi, mkv, mov, flv |
| 音频 | 🎵 | #F5F0FF (紫) | mp3, wav, flac, aac |
| 文档 | 📄 | #E6F6FF (蓝) | doc, docx, txt, rtf |
| 表格 | 📊 | #F0FFF0 (绿) | xls, xlsx, csv |
| 演示 | 📽️ | #FFF5E6 (橙) | ppt, pptx, key |
| PDF | 📕 | #FFF0F0 (红) | pdf |
| 代码 | 💻 | #F5F5F5 (灰) | cpp, py, js, html |
| 压缩包 | 📦 | #FFFBE6 (黄) | zip, rar, 7z, tar |
| 未知 | 📎 | #F5F5F5 (灰) | 其他 |

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl + A | 全选 |
| Ctrl + 点击 | 多选/取消选择 |
| Shift + 点击 | 范围选择 |
| Ctrl + C | 复制 |
| Ctrl + X | 剪切 |
| Ctrl + V | 粘贴 |
| Delete | 删除 |
| F2 | 重命名 |
| Space | 预览 |
| Ctrl + N | 新建文件夹 |
| Ctrl + F | 搜索 |
| F5 | 刷新 |
| Alt + ↑ | 上级目录 |
| Alt + ← | 后退 |
| Alt + → | 前进 |

---

## 加载与空态

### 加载状态

```mermaid
flowchart TD
    subgraph Skeleton["骨架屏"]
        subgraph Row1["第一行"]
            Card1["░░░░░░░░<br/>░░░░░░░░<br/>░░░░░░░░"]
            Card2["░░░░░░░░<br/>░░░░░░░░<br/>░░░░░░░░"]
            Card3["░░░░░░░░<br/>░░░░░░░░<br/>░░░░░░░░"]
            Card4["░░░░░░░░<br/>░░░░░░░░<br/>░░░░░░░░"]
        end
    end
```

### 空搜索结果

```mermaid
flowchart TD
    subgraph NoResults["无搜索结果"]
        Icon["🔍"]
        Title["未找到相关文件"]
        Hint["请尝试其他关键词搜索"]
        Action["[ 清除搜索 ]"]
    end
    
    Icon --> Title --> Hint --> Action
```

### 加载失败

```mermaid
flowchart TD
    subgraph LoadFail["加载失败状态"]
        Icon2["⚠️"]
        Title2["加载失败"]
        Hint2["网络连接失败，请检查网络"]
        Action2["[ 重新加载 ]"]
    end
    
    Icon2 --> Title2 --> Hint2 --> Action2
```

---

## 响应式适配

### Compact 模式 (< 900px)

- 网格视图: 3 列
- 列表视图: 隐藏"类型"和"修改时间"列
- 批量操作栏: 简化按钮
- 面包屑: 只显示当前目录名

### Expanded 模式 (> 1400px)

- 网格视图: 6-8 列
- 列表视图: 显示更多列（创建时间、访问时间）
- 可选右侧详情面板

---

## 相关文档

- [主窗口设计](main-window.md) - 整体窗口布局
- [首页设计](home.md) - 首页设计
- [界面设计规范](../qml/02-界面设计规范.md) - 设计系统

---

*版本: v2.0*
*最后更新: 2026-03-16*
*设计方向: 参考百度网盘现代化界面*
