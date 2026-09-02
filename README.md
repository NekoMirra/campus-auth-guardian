# Campus Auth Guardian

校园网认证守护程序 v2.0 — **Rust 内核 + WinUI 3 界面**，支持 x64 与 ARM64 原生双架构。

## 功能特性

- **现代化 WinUI 3 界面** — Mica 材质背景、NavigationView 导航、Fluent Design 控件、深浅色主题跟随
- **首页双状态卡** — 「Internet 连通性」与「Portal 认证」独立状态指示，一眼分清断网与未认证
- **首次使用向导** — 4 步引导配置（服务器检测 → 账号 → 真实验证 → 守护开关），服务器可达性实时探测
- **守护模式** — 后台周期检测，断网/未认证自动重连，指数退避（封顶 10 分钟）
- **运营商切换** — 校园网 / 中国移动 / 中国联通 / 中国电信，托盘与设置页双入口
- **固定 IP 回退** — 固定 IP 认证失败时自动回退自动检测 IP
- **托盘常驻** — 右键菜单（立即认证 / 守护开关 / 运营商子菜单 / 注销 / 显示主窗 / 退出）、双击唤醒主窗、气泡通知
- **开机自启** — 注册表 HKCU Run 项，UI 一键开关
- **运行日志** — 内存环形缓冲 + 文件落盘，512KB 自动轮转，UI 内实时查看
- **单实例** — 重复启动自动唤醒已运行窗口

## 系统要求

- Windows 10 1809+ / Windows 11（x64 或 ARM64 原生）
- Windows App SDK 1.8 运行时（未安装时程序会自动引导安装）

## 从源码构建

### 前置条件

- Rust 1.85+（msvc toolchain）+ `aarch64-pc-windows-msvc` target（构建 ARM64 时）
- .NET SDK 10.0
- Visual Studio 2022/2026（含 MSVC v145 工具集）或仅 MSBuild

### 构建

```bash
# Rust 内核（x64）
cargo build --release --target x86_64-pc-windows-msvc -p guardian-core

# Rust 内核（ARM64，需 clang-cl 交叉）
# 环境变量：CC=clang-cl CFLAGS=--target=aarch64-pc-windows-msvc
cargo build --release --target aarch64-pc-windows-msvc -p guardian-core

# C# WinUI 3 壳（自动编译 XAML、链接 Rust 内核、SelfContained 部署）
cd csharp
dotnet build -c Release -p:Platform=x64
dotnet build -c Release -p:Platform=ARM64
```

产物：

- `csharp/bin/x64/Release/net10.0-windows10.0.26100.0/CampusAuthGuardian.exe`
- `csharp/bin/ARM64/Release/net10.0-windows10.0.26100.0/CampusAuthGuardian.exe`

推送 `v*` 标签后，GitHub Actions 自动构建双架构并发布 Release。

## 配置

程序首次启动会进入**配置向导**；也可手动编辑 exe 同目录的 `config.ini`：

```ini
[network]
# ePortal 认证 API 地址
auth_url = http://10.10.102.50:801/eportal/portal/login
# 连通性检测地址
check_url = http://www.baidu.com
# 检测间隔（秒）
check_interval = 30

[account]
# 学号
student_id = 24208116
# 运营商：campus / cmcc / unicom / telecom
operator_type = campus
# 密码
user_password = YOUR_PASSWORD
# 固定 IP（可选；NAT 环境下填校园网侧 IP）
fixed_ip = 10.59.29.29

[guardian]
enabled = 0
retry_interval = 10
max_retries = 3
```

### 旧版配置兼容

v1.x 的 `user_account = ,0,学号@运营商` 格式自动解析迁移。

## 架构

```
campus-auth-guardian/
├── crates/guardian-core/     # Rust 认证内核（staticlib + cdylib + rlib）
│   └── src/
│       ├── auth.rs           # ePortal JSONP 认证协议
│       ├── netcheck.rs       # 连通性 / captive portal 检测
│       ├── config.rs         # INI 配置读写 + 旧格式兼容
│       ├── guardian.rs       # 守护循环（指数退避状态机）
│       ├── logger.rs         # 环形缓冲日志 + 文件轮转
│       ├── ipdetect.rs       # GetAdaptersAddresses 本机 IP 枚举
│       └── ffi.rs            # C ABI 导出层
└── csharp/                   # WinUI 3 壳（.NET 10）
    ├── MainWindow.xaml       # 状态页 / 设置页 / 日志页
    ├── WizardPage.xaml       # 首次使用向导
    ├── TrayIcon.cs           # 托盘图标 + 右键菜单 + 气泡
    └── Native.cs             # Rust FFI P/Invoke
```

## 日志

日志文件位于 exe 同目录 `campus_auth.log`，超过 512KB 自动轮转。

## License

MIT License
