# Campus Auth Guardian

校园网认证守护程序 - Windows 托盘应用

## 功能特性

- **系统托盘运行** - 后台静默运行，不占用任务栏
- **一键认证** - 右键菜单快速手动认证
- **守护模式** - 网络断开时自动重连，支持指数退避重试
- **运营商切换** - 托盘菜单快速切换校园网/移动/联通/电信
- **固定IP回退** - 固定IP认证失败时自动使用检测到的IP重试
- **Windows通知中心** - 通过 Action Center 显示通知，兼容 ARM64
- **开机自启** - 可选的 Windows 启动项集成
- **状态图标** - 直观显示连接状态
  - ✓ 绿色对号：已连接
  - 🛡 蓝色盾牌：守护模式监控中
  - ✗ 红色叉号：连接失败
- **内嵌配置模板** - 首次运行自动生成 config.ini，无需额外模板文件

## 系统要求

- Windows 10/11（Toast 通知需要 Windows 10+）
- MinGW-w64 或 MSVC 编译环境
- CMake 3.10+

## 编译

### MinGW

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

### MSVC

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### MSVC ARM64

```bash
mkdir build-arm64 && cd build-arm64
cmake .. -G "Visual Studio 17 2022" -A ARM64
cmake --build . --config Release
```

### 手动编译 (MinGW)

```bash
g++ -std=c++17 -o CampusAuthGuardian.exe src/main.cpp \
    -lwininet -lws2_32 -lcomctl32 -lshell32 -liphlpapi \
    -lole32 -lpropsys -lshlwapi \
    -mwindows
```

编译完成后可使用 `--console` 参数启动查看日志：

```bash
./CampusAuthGuardian.exe --console
```

## 配置

首次运行会自动在程序同目录生成 `config.ini`。

### 配置项说明

```ini
[network]
# 认证服务器地址（校园网门户登录API，注意端口号）
auth_url = http://10.10.102.50:801/eportal/portal/login
# 网络连通性检测地址
check_url = http://www.baidu.com
# 网络检测间隔（秒），守护模式下每隔此时间检测一次
check_interval = 30

[account]
# 学号
student_id = YOUR_STUDENT_ID
# 运营商类型：campus / cmcc / unicom / telecom
operator_type = unicom
# 账号密码
user_password = YOUR_PASSWORD
# 固定IP地址（可选，留空则自动检测）
fixed_ip =

[guardian]
# 是否默认启用守护模式 (0=关闭, 1=开启)
enabled = 0
# 重试间隔（秒）
retry_interval = 10
# 最大重试次数
max_retries = 3
```

### 运营商说明

| 运营商 | operator_type | 说明 |
|--------|---------------|------|
| 校园网 | `campus` | 学校自有网络 |
| 中国移动 | `cmcc` | 移动宽带 |
| 中国联通 | `unicom` | 联通宽带 |
| 中国电信 | `telecom` | 电信宽带 |

可在配置文件中设置，也可通过托盘菜单「切换运营商」实时切换。

### 旧版配置兼容

旧版使用 `user_account = ,0,学号@运营商` 格式，新版拆分为 `student_id` 和 `operator_type` 两个独立字段。程序会自动解析旧格式并兼容。

## 使用

1. 编辑 `config.ini` 填入学号、密码和运营商
2. 运行程序，最小化到托盘
3. 右键托盘图标使用菜单

### 托盘菜单

| 选项 | 说明 |
|------|------|
| 手动认证 | 立即执行一次认证 |
| 打开登录页 | 在浏览器打开校园网登录页 |
| 切换运营商 | 子菜单快速切换运营商类型 |
| 守护模式 | 开关守护模式（自动重连） |
| 注销登录 | 断开当前连接 |
| 打开配置文件 | 用默认编辑器打开 config.ini |
| 重新加载配置 | 热重载配置文件无需重启 |
| 开机自启 | 开关开机自启动 |
| 退出 | 退出程序 |

## 日志

日志文件位于程序同目录下的 `campus_auth.log`，包含每次认证请求的详细信息。日志超过 512KB 自动轮转。

## ARM64 兼容性

- 所有 Win32 API 调用使用 Unicode（W）版本，避免 ANSI 编码问题
- 配置文件读写使用 `GetPrivateProfileStringW`，正确处理中文
- Toast 通知通过动态加载 WinRT，兼容 x86/x64/ARM64

## 项目结构

```
campus-auth-guardian/
├── src/
│   └── main.cpp           # 主程序源码
├── CMakeLists.txt         # CMake 构建配置
├── config.ini.template    # 配置文件参考（可选，程序内嵌模板）
├── .gitignore
└── README.md
```

## License

MIT License
