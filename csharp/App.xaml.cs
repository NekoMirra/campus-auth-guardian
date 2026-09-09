using Microsoft.UI.Xaml;
using Microsoft.UI.Dispatching;
using Microsoft.Windows.AppLifecycle;
using System.Runtime.InteropServices;
using System.Collections.Concurrent;

namespace CampusAuthGuardian
{
    public partial class App : Application
    {
        private Window? _window;
        private static AppInstance? _mainInstance;
        private static DispatcherQueue? _dq;
        private static readonly ConcurrentDictionary<string, byte> _handles = new();
        private const string SingleInstanceKey = "CampusAuthGuardian-Main";

        /// 可写数据目录（配置/日志）。便携运行 = exe 同目录；安装到只读目录 = %APPDATA%。
        internal static string DataDir { get; private set; } = AppContext.BaseDirectory;
        internal static string ConfigPath { get; private set; } = Path.Combine(AppContext.BaseDirectory, "config.ini");

        /// 安装到 Program Files 等只读目录时回退 %APPDATA%，并迁移 exe 旁已有配置。
        private static void ResolveDataDir()
        {
            string exeDir = AppContext.BaseDirectory;
            string exeCfg = Path.Combine(exeDir, "config.ini");
            if (DirWritable(exeDir)) { DataDir = exeDir; ConfigPath = exeCfg; return; }
            string appDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "CampusAuthGuardian");
            try { Directory.CreateDirectory(appDir); } catch { }
            string appCfg = Path.Combine(appDir, "config.ini");
            try { if (File.Exists(exeCfg) && !File.Exists(appCfg)) File.Copy(exeCfg, appCfg); } catch { }
            DataDir = appDir; ConfigPath = appCfg;
        }

        private static bool DirWritable(string dir)
        {
            try
            {
                string probe = Path.Combine(dir, ".writetest");
                using (File.Create(probe)) { }
                File.Delete(probe);
                return true;
            }
            catch { return false; }
        }

        public App()
        {
            InitializeComponent();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            _dq = DispatcherQueue.GetForCurrentThread();
            ResolveDataDir();

            // 单实例（key-based）：拿不到 key = 已有实例在跑，重定向激活后退出
            _mainInstance = AppInstance.FindOrRegisterForKey(SingleInstanceKey);
            if (!_mainInstance.IsCurrent)
            {
                var ea = AppInstance.GetCurrent().GetActivatedEventArgs();
                try
                {
                    // 等待重定向送达（最多 1 秒），防止第二实例过早退出丢失唤醒
                    _mainInstance.RedirectActivationToAsync(ea).AsTask().WaitAsync(TimeSpan.FromSeconds(1));
                }
                catch { }
                Exit();
                return;
            }
            // 我是主实例：监听重定向（二次启动 -> 唤醒窗口）
            _mainInstance.Activated += OnRedirected;

            this.UnhandledException += (s, e) =>
            {
                try
                {
                    File.AppendAllText(Path.Combine(DataDir, "crash.log"),
                        $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {e.Message}\n{e.Exception?.StackTrace}\n\n");
                }
                catch { }
                e.Handled = true; // 阻止闪退；崩溃详情见 crash.log
            };

            // Rust 内核初始化（配置/日志与 ConfigPath 同目录）
            int rc = Native.GuardianInit(ConfigPath);
            if (rc != 0)
            {
                NativeWin32.MessageBoxW(IntPtr.Zero,
                    $"guardian_init 返回 {rc}：无法创建或读取 config.ini", "内核初始化失败", 0);
                Exit();
                return;
            }

            _window = new MainWindow();
            _window.Activate();
        }

        private void OnRedirected(object? sender, Microsoft.Windows.AppLifecycle.AppActivationArguments e)
        {
            // 二次启动唤醒：显示主窗
            _dq?.TryEnqueue(() =>
            {
                if (_window is null) return;
                _window.AppWindow.Show();
                _window.Activate();
            });
        }
    }
}

internal static partial class NativeWin32
{
    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);
}
