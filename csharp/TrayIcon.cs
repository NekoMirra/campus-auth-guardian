using System.Runtime.InteropServices;

namespace CampusAuthGuardian
{
    /// <summary>
    /// 托盘图标：右键菜单（立即认证/守护开关/运营商/注销/显示主窗/退出）+ 双击恢复主窗 + 气泡通知。
    /// </summary>
    internal sealed class TrayIcon : IDisposable
    {
        private readonly MainWindow _owner;
        private IntPtr _hWnd;
        private bool _added;
        private bool _guardianOn;
        private IntPtr _menu;

        public event Action? RestoreRequested;

        private const uint WM_APP_TRAY = 0x8000 + 0x5A1;
        private const uint WM_LBUTTONDBLCLK = 0x0203;
        private const uint WM_RBUTTONUP = 0x0205;
        private const uint WM_COMMAND = 0x0111;

        // 菜单 ID
        private const uint IDM_AUTH_NOW = 1001;
        private const uint IDM_GUARDIAN = 1002;
        private const uint IDM_OP_CAMPUS = 1010;
        private const uint IDM_OP_CMCC = 1011;
        private const uint IDM_OP_UNICOM = 1012;
        private const uint IDM_OP_TELECOM = 1013;
        private const uint IDM_LOGOUT = 1003;
        private const uint IDM_SHOW = 1004;
        private const uint IDM_EXIT = 1005;

        public TrayIcon(MainWindow owner)
        {
            _owner = owner;
            CreateWindow();
            AddIcon();
        }

        // Native
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct NOTIFYICONDATA
        {
            public uint cbSize;
            public IntPtr hWnd;
            public uint uID;
            public uint uFlags;
            public uint uCallbackMessage;
            public IntPtr hIcon;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string szTip;
            public uint dwState;
            public uint dwStateMask;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string szInfo;
            public uint uTimeout;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
            public string szInfoTitle;
            public uint dwInfoFlags;
        }

        private delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WNDCLASS
        {
            public uint style;
            public WndProcDelegate lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public IntPtr hInstance;
            public IntPtr hIcon;
            public IntPtr hCursor;
            public IntPtr hbrBackground;
            public string lpszMenuName;
            public string lpszClassName;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern ushort RegisterClassW(ref WNDCLASS lpWndClass);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateWindowExW(uint exStyle, string lpClassName, string lpWindowName,
            uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);

        [DllImport("user32.dll")]
        private static extern IntPtr DefWindowProcW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool DestroyWindow(IntPtr hWnd);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string? name);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA lpData);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr ExtractIconW(IntPtr hInst, string file, uint index);

        [DllImport("user32.dll")]
        private static extern bool DestroyIcon(IntPtr hIcon);

        [DllImport("user32.dll")]
        private static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool AppendMenuW(IntPtr menu, uint flags, UIntPtr id, string text);

        private static bool AppendMenuItem(IntPtr menu, uint flags, uint id, string text)
            => AppendMenuW(menu, flags, new UIntPtr(id), text);

        private static bool AppendPopup(IntPtr menu, IntPtr subMenu, string text)
            => AppendMenuW(menu, MF_POPUP, (UIntPtr)subMenu, text);

        [DllImport("user32.dll")]
        private static extern int TrackPopupMenuEx(IntPtr menu, uint flags, int x, int y, IntPtr hWnd, IntPtr lptpm);

        [DllImport("user32.dll")]
        private static extern bool DestroyMenu(IntPtr menu);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        private static extern bool GetCursorPos(out POINT lpPoint);

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int X; public int Y; }

        private const uint NIM_ADD = 0x0, NIM_MODIFY = 0x1, NIM_DELETE = 0x2;
        private const uint NIF_MESSAGE = 0x1, NIF_ICON = 0x2, NIF_TIP = 0x4, NIF_INFO = 0x10;
        private const uint NIIF_INFO = 0x1;
        private const uint MF_STRING = 0x0, MF_SEPARATOR = 0x800, MF_CHECKED = 0x8, MF_POPUP = 0x10;

        private WndProcDelegate? _proc;

        private void CreateWindow()
        {
            _proc = WndProc;
            var wc = new WNDCLASS
            {
                lpfnWndProc = _proc,
                lpszClassName = "CampusAuthGuardianTrayWnd",
                hInstance = GetModuleHandleW(null),
            };
            RegisterClassW(ref wc);
            _hWnd = CreateWindowExW(0, wc.lpszClassName, "CampusAuthGuardianTray",
                0x80000000 /* WS_POPUP */,
                0, 0, 0, 0, IntPtr.Zero, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
        }

        private IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
        {
            if (msg == WM_APP_TRAY)
            {
                uint mouse = (uint)(lParam.ToInt64() & 0xFFFF);
                if (mouse == WM_LBUTTONDBLCLK)
                {
                    RestoreRequested?.Invoke();
                }
                else if (mouse == WM_RBUTTONUP)
                {
                    ShowContextMenu();
                }
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }


        private void ShowContextMenu()
        {
            _menu = CreatePopupMenu();
            AppendMenuItem(_menu, MF_STRING, IDM_AUTH_NOW, "立即认证");
            AppendMenuItem(_menu, MF_STRING | (_guardianOn ? MF_CHECKED : 0u), IDM_GUARDIAN, "守护模式");
            AppendMenuItem(_menu, MF_SEPARATOR, 0, "");
            AppendPopup(_menu, BuildOperatorMenu(), "切换运营商");
            AppendMenuItem(_menu, MF_STRING, IDM_LOGOUT, "注销登录");
            AppendMenuItem(_menu, MF_SEPARATOR, 0, "");
            AppendMenuItem(_menu, MF_STRING, IDM_SHOW, "显示主窗口");
            AppendMenuItem(_menu, MF_STRING, IDM_EXIT, "退出");

            GetCursorPos(out var pt);
            SetForegroundWindow(_hWnd);
            // TPM_RETURNCMD：同步返回选中 ID，绕开 WM_COMMAND 派发（WinUI Dispatcher 不泵外部窗口）
            int cmd = TrackPopupMenuEx(_menu, 0x0100 | 0x0180,
                pt.X, pt.Y, _hWnd, IntPtr.Zero);
            DestroyMenu(_menu);
            if (cmd != 0)
            {
                HandleMenu((uint)cmd);
            }
        }

        private IntPtr BuildOperatorMenu()
        {
            IntPtr sub = CreatePopupMenu();
            string current = ReadOperator();
            AppendMenuItem(sub, MF_STRING | (current == "campus" ? MF_CHECKED : 0u), IDM_OP_CAMPUS, "校园网");
            AppendMenuItem(sub, MF_STRING | (current == "cmcc" ? MF_CHECKED : 0u), IDM_OP_CMCC, "中国移动");
            AppendMenuItem(sub, MF_STRING | (current == "unicom" ? MF_CHECKED : 0u), IDM_OP_UNICOM, "中国联通");
            AppendMenuItem(sub, MF_STRING | (current == "telecom" ? MF_CHECKED : 0u), IDM_OP_TELECOM, "中国电信");
            return sub;
        }

        private static string ReadOperator()
        {
            string s = Native.ConfigJson();
            int p = s.IndexOf("\"operator\":\"", StringComparison.Ordinal);
            if (p < 0) return "campus";
            p += 12;
            int e = s.IndexOf('"', p);
            return e > p ? s[p..e] : "campus";
        }

        private void HandleMenu(uint id)
        {
            switch (id)
            {
                case IDM_AUTH_NOW:
                    Task.Run(() => Native.AuthNow());
                    break;
                case IDM_GUARDIAN:
                    _guardianOn = !_guardianOn;
                    Native.GuardianSetEnabled(_guardianOn ? 1 : 0);
                    break;
                case IDM_OP_CAMPUS: Native.SetOperator("campus"); break;
                case IDM_OP_CMCC: Native.SetOperator("cmcc"); break;
                case IDM_OP_UNICOM: Native.SetOperator("unicom"); break;
                case IDM_OP_TELECOM: Native.SetOperator("telecom"); break;
                case IDM_LOGOUT:
                    string base_ = JsonPick(Native.ConfigJson(), "auth_url");
                    int ep = base_.IndexOf("/eportal");
                    if (ep > 0)
                    {
                        string logoutUrl = base_[..ep] + "/eportal/portal/logout?callback=dr1005";
                        Task.Run(async () =>
                        {
                            try
                            {
                                using var http = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(8) };
                                await http.GetAsync(logoutUrl);
                            }
                            catch { }
                        });
                    }
                    break;
                case IDM_SHOW:
                    RestoreRequested?.Invoke();
                    break;
                case IDM_EXIT:
                    // 优雅退出：停守护 -> 删托盘 -> 关主窗（触发 Closed 清理链）
                    Native.GuardianSetEnabled(0);
                    Dispose();
                    _owner.DispatcherQueue.TryEnqueue(() => _owner.Close());
                    break;
            }
        }

        private static string JsonPick(string s, string key)
        {
            int p = s.IndexOf($"\"{key}\":", StringComparison.Ordinal);
            if (p < 0) return "";
            p += key.Length + 3;
            if (p < s.Length && s[p] == '"')
            {
                int e = s.IndexOf('"', p + 1);
                return e > p ? s[(p + 1)..e] : "";
            }
            int e2 = s.IndexOfAny(new[] { ',', '}' }, p);
            return e2 > p ? s[p..e2] : "";
        }

        private void AddIcon()
        {
            // 从 exe 内嵌图标提取（ApplicationIcon 编译进 exe）
            var exe = Environment.ProcessPath ?? "";
            IntPtr hIcon = ExtractIconW(GetModuleHandleW(null), exe, 0);
            var nid = new NOTIFYICONDATA
            {
                cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATA>(),
                hWnd = _hWnd,
                uID = 1,
                uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON,
                uCallbackMessage = WM_APP_TRAY,
                hIcon = hIcon,
                szTip = "校园网认证守护\n双击显示主窗",
            };
            _added = Shell_NotifyIcon(NIM_ADD, ref nid);
        }

        public void ShowBalloon(string title, string body)
        {
            if (!_added) AddIcon();
            if (!_added) return;
            var nid = new NOTIFYICONDATA
            {
                cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATA>(),
                hWnd = _hWnd,
                uID = 1,
                uFlags = NIF_INFO,
                szInfoTitle = title,
                szInfo = body,
                dwInfoFlags = NIIF_INFO,
            };
            Shell_NotifyIcon(NIM_MODIFY, ref nid);
        }

        public void SetGuardianState(bool on) => _guardianOn = on;

        public void Dispose()
        {
            if (_added)
            {
                var nid = new NOTIFYICONDATA
                {
                    cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATA>(),
                    hWnd = _hWnd,
                    uID = 1,
                };
                Shell_NotifyIcon(NIM_DELETE, ref nid);
                _added = false;
            }
        }
    }
}
