# 设置页面设计 (v2.0)

## 概述

设置页面用于管理 Disk 客户端的配置选项，采用分类卡片式布局，参考百度网盘设置界面，提供清晰的功能分组和直观的操作体验。

**视觉风格**: 分类卡片 + 简洁表单 + 开关控制

---

## 整体布局

```mermaid
flowchart TB
    subgraph SettingsPage["设置页面整体布局"]
        subgraph Header["标题栏"]
            Title["设置"]
        end
        
        subgraph MainContent["主内容区"]
    subgraph SideNav["侧边导航"]
        Nav1["👤 账户信息 ✓"]
        Nav2["🔔 通知设置"]
        Nav3["⚙ 通用设置"]
        Nav4["🔌 传输设置"]
        Nav5["🌐 网络设置"]
        Nav6["💻 关于"]
            end
            
            subgraph ContentArea["内容区"]
                subgraph AccountSection["账户信息"]
                    Avatar["头像           [👤] 用户名"]
                    Email["邮箱           user@example.com"]
                    Storage["存储空间       [████████░░░░░░░░░░] 25%"]
                    StorageInfo["已用 2.5 GB / 10 GB"]
                    AccountButtons["[修改密码]  [退出登录]"]
                end
                
                subgraph MembershipSection["会员信息"]
                    CurrentPlan["当前套餐: 免费版"]
                    StorageLimit["存储空间: 10 GB"]
                    UpgradeBtn["[升级会员] - 获取更多存储空间"]
                end
            end
        end
        
        BottomBar["                              [恢复默认]    [保存设置]"]
    end
```

---

## 侧边导航

### 导航项

```mermaid
flowchart TD
    subgraph SideNavFull["侧边导航"]
        Nav1["👤 账户信息"]
        Nav2["🔔 通知设置"]
        Nav3["⚙ 通用设置"]
        Nav4["🔌 传输设置"]
        Nav5["🌐 网络设置"]
        Nav6["💻 关于"]
    end
```

### 导航项规格

| 属性 | 值 |
|------|------|
| 宽度 | 200px |
| 高度 | 44px |
| 图标 | 20px |
| 文字 | 14px |
| 选中 | Primary 文字 + Light 背景 + 左侧指示条 |

---

## 账户信息设置

### 账户卡片

```mermaid
flowchart TD
    subgraph AccountCard["账户信息卡片"]
        Header1["账户信息"]

        subgraph AvatarSection["头像与用户名"]
            AvatarBox["👤 头像"]
            UsernameBox["用户名: 用户名"]
            ChangeAvatar["[更换头像]"]
        end

        subgraph ContactSection["联系方式"]
            EmailBox["邮箱: user@example.com"]
            NicknameBox["昵称: 用户昵称"]
        end

        ActionButtons["[修改密码]  [退出登录]"]
    end
```

### 存储空间卡片

```mermaid
flowchart TD
    subgraph StorageCard["存储空间卡片"]
        Header2["存储空间"]

        subgraph StorageChart["存储图表"]
            PieChart["已用 25%"]
        end

        UsageInfo["已用 2.5 GB / 10 GB"]

        subgraph CategoryBreakdown["分类统计"]
            Cat1["📷 图片 (2.5 GB)"]
            Cat2["🎬 视频 (8.2 GB)"]
            Cat3["📄 文档 (1.2 GB)"]
            Cat4["🎵 音频 (3.1 GB)"]
            Cat5["📦 其他 (156 MB)"]
        end

        ActionBtns2["[管理存储空间]  [购买扩容]"]
    end
```

---

## 通知设置

```mermaid
flowchart TD
    subgraph NotificationSettings["通知设置"]
        Header3["通知设置"]

        subgraph Setting1["系统通知"]
            Toggle1["☑️ 启用系统通知"]
            Desc1["      接收文件传输完成、分享访问等重要通知"]
        end

        subgraph Setting2["提示音"]
            Toggle2["☐ 传输完成时播放提示音"]
            Desc2["      文件上传/下载完成时播放提示音效"]
        end

        subgraph Setting3["桌面通知"]
            Toggle3["☑️ 显示桌面通知"]
            Desc3["      在屏幕右下角显示通知弹窗"]
        end

        subgraph Setting4["存储提醒"]
            Toggle4["☑️ 存储空间不足提醒"]
            Desc4["      存储空间使用超过 80% 时提醒"]
            Threshold4["      提醒阈值: [80] %"]
        end

        subgraph Setting5["清理提醒"]
            Toggle5["☑️ 回收站自动清理提醒"]
            Desc5["      回收站文件即将自动删除前提醒"]
            Days5["      提前 [3] 天提醒"]
        end
    end
```

---

## 通用设置

```mermaid
flowchart TD
    subgraph GeneralSettings["通用设置"]
        Header4["通用设置"]

        subgraph StartupSettings["启动设置"]
            SectionTitle1["启动设置"]
            Toggle1["☑️ 开机自动启动"]
            Toggle2["☑️ 启动时最小化到托盘"]
            Toggle3["☐ 启动时自动开始未完成的传输"]
        end

        subgraph CloseBehavior["关闭行为"]
            SectionTitle2["关闭行为"]
            Option1["● 最小化到系统托盘"]
            Option2["○ 直接退出应用"]
        end

        subgraph FileManagement["文件管理"]
            SectionTitle3["文件管理"]
            Toggle4["☑️ 删除文件前确认"]
            Toggle5["☑️ 清空回收站前确认"]
            Toggle6["☑️ 双击文件时下载到本地"]
            Option3["    ○ 直接打开文件 (如果支持)"]
        end

        subgraph LanguageSettings["语言设置"]
            SectionTitle4["语言设置"]
            Select4["界面语言:  [简体中文 ▼]"]
            Options4["             • 简体中文<br/>             • English"]
        end
    end
```

---

## 传输设置

```mermaid
flowchart LR
    subgraph BottomActionBar["底部操作栏"]
        Buttons["                                    [恢复默认]    [取消]    [保存设置]"]
    end
```

---

## 网络设置

```mermaid
flowchart TD
    subgraph NetworkSettings["网络设置"]
        Header6["网络设置"]

        subgraph ServerSettings["服务器设置"]
            SectionTitle1["服务器设置"]
            ServerAddress["服务器地址: https://disk.example.com"]
            TestConnection["[测试连接]"]
            Result["✅ 连接成功 (延迟 24ms)"]
        end

        subgraph ProxySettings["代理设置"]
            SectionTitle2["代理设置"]
            NoProxy["● 不使用代理"]
            UseSystemProxy["○ 使用系统代理"]
            CustomProxy["○ 自定义代理<br/>                类型:  [HTTP ▼]<br/>                地址:  [127.0.0.1                    ]                            端口:  [8080   ]"]
        end

        subgraph TimeoutSettings["超时设置"]
            SectionTitle3["超时设置"]
            ConnectTimeout["连接超时:  [30] 秒"]
            TransferTimeout["传输超时:  [300] 秒"]
        end
    end
```

---

## 关于页面

```mermaid
flowchart TD
    subgraph AboutPage["关于页面"]
        Header7["关于 Disk"]

        subgraph LogoSection["Logo区域"]
            Logo["🌀 Disk Logo"]
        end

        AppInfo["Disk 桌面客户端<br/>版本 1.0.0 (Build 20260315)"]

        Links["官方网站:  https://disk.example.com<br/>帮助中心:  https://help.disk.example.com<br/>反馈建议:  feedback@disk.example.com"]

        CheckUpdate["[检查更新]"]
        UpdateStatus["✅ 当前已是最新版本<br/>上次检查: 2026-03-16"]

        Copyright["© 2026 Disk Team. All rights reserved.<br/>开源协议: MIT License"]
    end
```

---

## 底部操作栏

```mermaid
flowchart TD
    subgraph TransferSettings["传输设置"]
        Header5["传输设置"]

        subgraph DownloadSettings["下载设置"]
            SectionTitle1["下载设置"]
            DownloadPath["默认下载位置: [浏览...]"]
            Checkbox1["☑️ 下载完成后自动打开文件夹"]
            Checkbox2["☑️ 下载完成后自动打开文件 (如果支持)"]

        subgraph UploadSettings["上传设置"]
            SectionTitle2["上传设置"]
            Toggle6["☑️ 开启秒传功能<br/>      通过文件哈希快速上传已存在的文件"]
            Toggle7["☑️ 上传完成后保留本地文件"]
        end

        subgraph ConcurrencySettings["并发设置"]
            SectionTitle3["并发设置"]
            UploadTasks["同时上传任务数:  [ 3 ▼]<br/>                   • 1<br/>                   • 3<br/>                   • 5<br/>                   • 10<br/>                ]
            DownloadTasks["同时下载任务数:  [ 3 ▼]<br/>                    • 1<br/>                    • 3<br/>                    • 5<br/>                    • 10<br/>               }
            ConcurrencyLabel["                  同时上传: 3 / 同时下载: 3"]
        end

        subgraph SpeedLimitSettings["速度限制"]
            SectionTitle4["速度限制"]
            LimitUpload["☐ 限制上传速度<br/>     [1000] KB/s"]
            LimitDownload["☐ 限制下载速度<br/>     [1000] KB/s"]
        end
    end
```

### 按钮说明

| 按钮 | 功能 |
|------|------|
| 恢复默认 | 重置当前分类设置为默认值 |
| 取消 | 放弃修改，返回上一页 |
| 保存设置 | 保存所有修改 |

---

## 设置保存反馈

### 保存成功

```mermaid
flowchart LR
    subgraph SaveSuccessToast["保存成功提示"]
        Toast7["✅ 设置已保存"]
    end
```

- 位置: 页面右上角
- 时长: 3 秒后自动消失
- 动画: 淡入淡出

---

## 相关文档

- [主窗口设计](main-window.md) - 整体窗口布局
- [界面设计规范](../qml/02-界面设计规范.md) - 设计系统

---

*版本: v2.0*
*最后更新: 2026-03-16*
*设计方向: 参考百度网盘现代化界面*
