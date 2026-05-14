# Campus Auth Guardian

校园网认证守护程序 - Windows 托盘应用

## 功能特性

- **系统托盘运行** - 后台静默运行，不占用任务栏
- **一键认证** - 右键菜单快速手动认证
- **守护模式** - 网络断开时自动重连，支持多级重试
- **开机自启** - 可选的 Windows 启动项集成
- **状态图标** - 直观显示连接状态
  - ✓ 绿色对号：已连接
  - 🛡 蓝色盾牌：守护模式监控中
  - ✗ 红色叉号：连接失败

## 系统要求

- Windows 7/8/10/11
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

### 手动编译 (MinGW)

```bash
g++ -std=c++17 -o CampusAuthGuardian.exe src/main.cpp \
    -lwininet -lws2_32 -lcomctl32 -lshell32 -liphlpapi \
    -mwindows
```

编译完成后可使用 `--console` 参数启动查看日志：

```bash
./CampusAuthGuardian.exe --console
```

## 配置

首次运行会自动从 `config.ini.template` 复制创建 `config.ini`。

### 配置项说明

```ini
[network]
# 认证服务器地址（校园网门户URL）
auth_url = http://10.10.102.50/eportal/portal/login
# 网络连通性检测地址（需使用能直连的外网）
check_url = http://www.baidu.com
# 网络检测间隔（秒），守护模式下每隔此时间检测一次
check_interval = 30

[account]
# 账号格式：,0,学号@运营商
# 运营商可选：unicom(联通), telecom(电信), cmcc(移动)
user_account = ,0,YOUR_STUDENT_ID@unicom
user_password = YOUR_PASSWORD

[guardian]
# 是否默认启用守护模式 (0=关闭, 1=开启)
enabled = 0
# 重试间隔（秒），认证失败后等待多久再试
retry_interval = 10
# 最大重试次数
max_retries = 3
```

### 运营商账号格式

| 运营商 | 格式 | 示例 |
|--------|------|------|
| 联通 | `,0,学号@unicom` | `,0,24028116@unicom` |
| 电信 | `,0,学号@telecom` | `,0,24028116@telecom` |
| 移动 | `,0,学号@cmcc` | `,0,24028116@cmcc` |

## 使用

1. 编辑 `config.ini` 填入账号信息
2. 运行程序，最小化到托盘
3. 右键托盘图标使用菜单

### 托盘菜单

| 选项 | 说明 |
|------|------|
| Manual Auth | 立即执行一次认证 |
| Open Login Page | 在浏览器打开登录页面 |
| Guardian Mode | 开关守护模式（自动重连） |
| Start with Windows | 开关开机自启 |
| Exit | 退出程序 |

## 日志

日志文件位于程序同目录下的 `campus_auth.log`，包含每次认证请求的详细信息。

## 项目结构

```
campus-auth-guardian/
├── src/
│   └── main.cpp          # 主程序源码
├── CMakeLists.txt       # CMake 构建配置
├── config.ini.template  # 配置文件模板
├── .gitignore
└── README.md
```

## License

MIT License