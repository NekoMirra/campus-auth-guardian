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

        public App()
        {
            InitializeComponent();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            _dq = DispatcherQueue.GetForCurrentThread();

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
                    File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "crash.log"),
                        $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {e.Message}\n{e.Exception?.StackTrace}\n\n");
                }
                catch { }
                e.Handled = true; // 阻止闪退；崩溃详情见 crash.log
            };

            // Rust 内核初始化（配置/日志在 exe 同目录）
            string exeDir = AppContext.BaseDirectory;
            string cfgPath = Path.Combine(exeDir, "config.ini");
            int rc = Native.GuardianInit(cfgPath);
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
