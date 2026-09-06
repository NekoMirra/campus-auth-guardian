using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Windowing;

namespace CampusAuthGuardian
{
    public sealed partial class MainWindow : Window
    {
        private readonly DispatcherTimer _timer;
        private readonly TrayIcon _tray;
        private bool _suppressToggle;
        private int _lastOperatorIdx = -1;

        public MainWindow()
        {
            InitializeComponent();
            Title = "校园网认证守护 CampusAuthGuardian";

            // Mica 现代背景
            ExtendsContentIntoTitleBar = true;
            try
            {
                var backdrop = new Microsoft.UI.Xaml.Media.MicaBackdrop();
                SystemBackdrop = backdrop;
            }
            catch { /* 旧系统回退纯色 */ }

            // 内容留出标题栏
            if (Content is FrameworkElement fe)
            {
                fe.Margin = new Thickness(0, 32, 0, 0);
            }

            _tray = new TrayIcon(this);
            _tray.RestoreRequested += () => DispatcherQueue.TryEnqueue(ShowFromTray);

            _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(400) };
            _timer.Tick += PollEvents;
            _timer.Start();

            // 关闭 = 隐藏到托盘（AppWindow.Closing 可取消）
            AppWindow.Closing += (s, e) =>
            {
                e.Cancel = true;
                DispatcherQueue.TryEnqueue(() => { this.AppWindow.Hide(); });
            };

            LoadSettings();
            RefreshStatus();
            RefreshLogs();
            Nav.SelectedItem = Nav.MenuItems[0];

            // 首次启动检测：学号为空则显示覆盖式向导
            string cfg = Native.ConfigJson();
            if (string.IsNullOrEmpty(JsonPick(cfg, "student_id")))
            {
                ShowWizardOverlay();
            }
        }

        private void ShowWizardOverlay()
        {
            WizardFrame.Content = new WizardPage(this);
            WizardFrame.Visibility = Visibility.Visible;
        }

        internal void HideWizardOverlay()
        {
            WizardFrame.Visibility = Visibility.Collapsed;
            WizardFrame.Content = null;
            LoadSettings();
            RefreshStatus();
        }

        internal void FinishWizard() => HideWizardOverlay();

        private void ShowFromTray()
        {
            Activate();
            AppWindow.Show();
        }

        // ---------- 事件轮询 ----------

        private void PollEvents(object? sender, object? e)
        {
            try
            {
            int guard = 0;
            while (guard++ < 64 && Native.TryPollEvent(out string? raw) && raw != null)
            {
                try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "ui_debug.log"),
                    $"{DateTime.Now:HH:mm:ss.fff} poll: {raw}\n"); } catch { }
                try
                {
                    using var doc = JsonDocument.Parse(raw);
                    var root = doc.RootElement;
                    string type = root.TryGetProperty("type", out var t) ? t.GetString() ?? "" : "";

                    if (type == "net")
                    {
                        string status = root.TryGetProperty("status", out var s) ? s.GetString() ?? "" : "";
                        var detail = root.TryGetProperty("detail", out var d) ? d.GetString() : null;

                        // Internet 连通性卡
                        (Windows.UI.Color netColor, string netText, string netSub) = status switch
                        {
                            "connected" => (MSGB(0x6C, 0xB8, 0x3F), "已连通", "可直接访问互联网"),
                            "captive" => (MSGB(0xFF, 0xA5, 0x00), "已连校园网·未认证", "点击「立即认证」完成 Portal 登录"),
                            "dns_pending" => (MSGB(0xFF, 0xA5, 0x00), "认证生效中", "DNS 尚未就绪，正在自动复检…"),
                            _ => (MSGB(0xE5, 0x4C, 0x52), "不可达", "无法连接检测服务器"),
                        };
                        NetLight.Fill = new Microsoft.UI.Xaml.Media.SolidColorBrush(netColor);
                        NetStatusText.Text = netText;
                        NetDetailText.Text = status switch
                        {
                            "connected" => netSub,
                            "captive" => "门户：" + Truncate(string.IsNullOrEmpty(detail) ? "检测到重定向" : detail!, 60),
                            "dns_pending" => netSub,
                            _ => "无法连接检测服务器" + (string.IsNullOrEmpty(detail) ? "" : "：" + Truncate(detail!, 60)),
                        };

                        // Portal 认证卡
                        (Windows.UI.Color authColor, string authText, string authSub) = status switch
                        {
                            "connected" => (MSGB(0x6C, 0xB8, 0x3F), "已认证", "ePortal 会话有效"),
                            "captive" => (MSGB(0xE5, 0x4C, 0x52), "未认证", "点击「立即认证」完成登录"),
                            "dns_pending" => (MSGB(0xFF, 0xA5, 0x00), "生效中", "认证已通过，网络正在生效"),
                            _ => (MSGB(0x80, 0x80, 0x80), "无法判断", "请先检查网络连接"),
                        };
                        AuthLight.Fill = new Microsoft.UI.Xaml.Media.SolidColorBrush(authColor);
                        AuthStatusText.Text = authText;
                        AuthDetailText.Text = authSub;

                        // 顶部摘要
                        StatusIcon.Glyph = status switch
                        {
                            "connected" => "\uE8D7",
                            "captive" => "\uE7BA",
                            _ => "\uE74D",
                        };
                        StatusText.Text = status switch
                        {
                            "connected" => "网络已连接",
                            "captive" => "需要认证",
                            _ => "网络不可达",
                        };
                        StatusLight.Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(netColor);
                        if (status == "connected") DetailText.Text = "认证有效，网络畅通";
                        else if (status == "captive") DetailText.Text = "检测到认证门户，需要 Portal 登录";
                        else DetailText.Text = "无法连接检测服务器";
                    }
                    else if (type == "auth")
                    {
                        bool ok = root.TryGetProperty("ok", out var okEl)
                            && (okEl.ValueKind == JsonValueKind.True
                                || (okEl.ValueKind == JsonValueKind.String && okEl.GetString() == "true"));
                        string detail = root.TryGetProperty("detail", out var m) ? m.GetString() ?? "" : "";
                        if (ok)
                        {
                            StatusIcon.Glyph = "\uE8D7";
                            StatusText.Text = "认证成功";
                            StatusLight.Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(MSGB(0x6C, 0xB8, 0x3F));
                            DetailText.Text = detail == "already_online" ? "本机已在线（无需重复认证）" : "Portal 协议认证成功";
                            _tray.ShowBalloon("校园网认证", detail == "already_online" ? "已在线，无需重复认证" : "认证成功，网络已连接");
                        }
                        else
                        {
                            _tray.ShowBalloon("校园网认证失败", string.IsNullOrEmpty(detail) ? "认证未成功" : Truncate(detail, 120));
                        }
                        RefreshLogs();
                    }
                    else if (type == "state")
                    {
                        // state 兼容数字与字符串两种 JSON 类型
                        int stNum = 0;
                        if (root.TryGetProperty("state", out var stEl))
                        {
                            stNum = stEl.ValueKind == System.Text.Json.JsonValueKind.Number
                                ? stEl.GetInt32()
                                : (int.TryParse(stEl.GetString(), out var n) ? n : 0);
                        }
                        bool running = stNum != 0;
                        if (!_suppressToggle && GuardianToggle.IsOn != running)
                        {
                            _suppressToggle = true;
                            GuardianToggle.IsOn = running;
                            _suppressToggle = false;
                        }
                        RefreshStatus();
                    }
                    else if (type == "config")
                    {
                        LoadSettings();
                    }
                }
                catch (JsonException) { }
            }
            }
            catch (Exception ex)
            {
                try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "ui_debug.log"),
                    $"{DateTime.Now:HH:mm:ss.fff} POLL-CRASH: {ex.Message} :: {ex.StackTrace?.Split('\n')[0]}\n"); } catch { }
            }
        }

        private static Windows.UI.Color MSGB(byte r, byte g, byte b) => Windows.UI.Color.FromArgb(0xFF, r, g, b);

        private static string Truncate(string s, int n) => s.Length <= n ? s : s[..n] + "…";

        private void RefreshStatus()
        {
            int st = Native.GuardianState();
            DetailText.Text = st switch
            {
                1 => "守护模式监控中，断网将自动重连",
                2 => "正在认证…",
                _ => "守护模式未启动",
            };
            bool running = st is 1 or 2;
            if (!_suppressToggle && GuardianToggle.IsOn != running)
            {
                _suppressToggle = true;
                GuardianToggle.IsOn = running;
                _suppressToggle = false;
            }

            string op = JsonPick(Native.ConfigJson(), "operator");
            int idx = op switch
            {
                "campus" => 0,
                "cmcc" => 1,
                "unicom" => 2,
                _ => 3,
            };
            if (_lastOperatorIdx != idx && OperatorBox.SelectedIndex != idx)
            {
                _lastOperatorIdx = idx;
                OperatorBox.SelectedIndex = idx;
            }
        }

        private void RefreshLogs()
        {
            string raw = Native.RecentLogs();
            var sb = new StringBuilder();
            try
            {
                using var doc = JsonDocument.Parse(raw);
                foreach (var el in doc.RootElement.EnumerateArray())
                {
                    string level = el.TryGetProperty("level", out var lv) ? lv.GetString() ?? "" : "";
                    string text = el.TryGetProperty("text", out var tx) ? tx.GetString() ?? "" : "";
                    string prefix = level switch
                    {
                        "WARN" => "⚠ ",
                        "ERROR" => "✗ ",
                        _ => "· ",
                    };
                    sb.AppendLine(prefix + text);
                }
            }
            catch (JsonException) { sb.Append(raw); }
            LogsText.Text = sb.ToString();
            LogsScroll.UpdateLayout();
            if (AutoScrollCheck?.IsChecked == true)
            {
                LogsScroll.ChangeView(null, float.MaxValue, null, true);
            }
        }

        private void LoadSettings()
        {
            string s = Native.ConfigJson();
            AuthUrlBox.Text = JsonPick(s, "auth_url");
            CheckUrlBox.Text = JsonPick(s, "check_url");
            StudentIdBox.Text = JsonPick(s, "student_id");
            PasswordBox.Password = JsonPick(s, "password");
            FixedIpBox.Text = JsonPick(s, "fixed_ip");
            CheckIntervalBox.Value = TryNum(s, "check_interval", 30);
            RetryIntervalBox.Value = TryNum(s, "retry_interval", 10);
            MaxRetriesBox.Value = TryNum(s, "max_retries", 3);
            bool autoOn = Native.GuardianIsAutostart() == 1;
            if (AutostartToggle.IsOn != autoOn)
            {
                _suppressToggle = true;
                AutostartToggle.IsOn = autoOn;
                _suppressToggle = false;
            }
        }

        private static string JsonPick(string s, string key)
        {
            try
            {
                using var doc = JsonDocument.Parse(s);
                if (doc.RootElement.TryGetProperty(key, out var v))
                {
                    return v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : v.ToString();
                }
            }
            catch (JsonException) { }
            return "";
        }

        private static double TryNum(string s, string key, double fallback)
        {
            string v = JsonPick(s, key);
            return double.TryParse(v, out var d) ? d : fallback;
        }

        // ---------- 事件处理 ----------

        private void Nav_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
        {
            if (args.SelectedItem is not NavigationViewItem item) return;
            string tag = item.Tag?.ToString() ?? "status";
            StatusPage.Visibility = tag == "status" ? Visibility.Visible : Visibility.Collapsed;
            SettingsPage.Visibility = tag == "settings" ? Visibility.Visible : Visibility.Collapsed;
            LogsPage.Visibility = tag == "logs" ? Visibility.Visible : Visibility.Collapsed;
            AboutFrame.Visibility = tag == "about" ? Visibility.Visible : Visibility.Collapsed;
            if (tag == "about" && AboutFrame.Content is null)
            {
                AboutFrame.Content = new AboutPage();
            }
            if (tag == "logs") RefreshLogs();
        }

        private void AuthNow_Click(object sender, RoutedEventArgs e)
        {
            StatusText.Text = "正在认证…";
            StatusLight.Foreground = new Microsoft.UI.Xaml.Media.SolidColorBrush(MSGB(0x00, 0x7E, 0xD4));
            Task.Run(() => Native.AuthNow());
        }

        private void Guardian_Toggled(object sender, RoutedEventArgs e)
        {
            if (_suppressToggle) return;
            Native.GuardianSetEnabled(GuardianToggle.IsOn ? 1 : 0);
        }

        private void OpenLoginPage_Click(object sender, RoutedEventArgs e)
        {
            string url = JsonPick(Native.ConfigJson(), "auth_url");
            int ep = url.IndexOf("/eportal", StringComparison.Ordinal);
            if (ep > 0) url = url[..ep];
            if (!string.IsNullOrEmpty(url))
            {
                try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
            }
        }

        private void Logout_Click(object sender, RoutedEventArgs e)
        {
            // ePortal 注销：logout URL 直接 GET
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
                    Native.AuthNow(); // 触发一次状态同步
                });
            }
        }

        private void Operator_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (OperatorBox.SelectedItem is not ComboBoxItem item) return;
            string op = item.Tag?.ToString() ?? "campus";
            Native.SetOperator(op);
        }

        private void Autostart_Toggled(object sender, RoutedEventArgs e)
        {
            if (_suppressToggle) return;
            Native.GuardianSetAutostart(AutostartToggle.IsOn ? 1 : 0);
        }

        private void Save_Click(object sender, RoutedEventArgs e)
        {
            static string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
            // 读取当前内核配置，用 JSON 逐字段合并（避免覆盖向导/托盘写入的字段）
            string current = Native.ConfigJson();
            string guardian = JsonPick(current, "guardian_enabled");
            string json = "{"
                + $"\"auth_url\":\"{Esc(AuthUrlBox.Text)}\","
                + $"\"check_url\":\"{Esc(CheckUrlBox.Text)}\","
                + $"\"student_id\":\"{Esc(StudentIdBox.Text)}\","
                + $"\"password\":\"{Esc(PasswordBox.Password)}\","
                + $"\"fixed_ip\":\"{Esc(FixedIpBox.Text)}\","
                + $"\"check_interval\":{(long)CheckIntervalBox.Value},"
                + $"\"retry_interval\":{(long)RetryIntervalBox.Value},"
                + $"\"max_retries\":{(long)MaxRetriesBox.Value},"
                + $"\"operator\":\"{Esc(JsonPick(current, "operator"))}\","
                + $"\"guardian_enabled\":{(guardian == "1" ? "true" : "false")}}}";
            int rc = Native.ConfigApply(json);
            InfoBar.IsOpen = true;
            InfoBar.Severity = rc == 0 ? InfoBarSeverity.Success : InfoBarSeverity.Error;
            InfoBar.Message = rc == 0 ? "设置已保存并即时生效" : $"保存失败（{rc}）";
            if (rc == 0) RefreshStatus();
        }

        private void OpenConfig_Click(object sender, RoutedEventArgs e)
        {
            string cfg = Path.Combine(AppContext.BaseDirectory, "config.ini");
            try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("notepad.exe", $"\"{cfg}\"")); } catch { }
        }

        private void ReloadConfig_Click(object sender, RoutedEventArgs e)
        {
            // 重读磁盘 ini：从文件解析后回填内核（热重载）
            string exeDir = AppContext.BaseDirectory;
            try
            {
                string text = File.ReadAllText(Path.Combine(exeDir, "config.ini"));
                string json = IniToJson(text);
                Native.ConfigApply(json);
                LoadSettings();
                RefreshStatus();
            }
            catch { }
        }

        private static string IniToJson(string ini)
        {
            static string Val(string text, string key)
            {
                foreach (var line in text.Split('\n'))
                {
                    var l = line.Trim();
                    if (l.StartsWith(key, StringComparison.OrdinalIgnoreCase))
                    {
                        int eq = l.IndexOf('=');
                        if (eq > 0) return l[(eq + 1)..].Trim();
                    }
                }
                return "";
            }
            static string EscQ(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
            string auth = Val(ini, "auth_url");
            string check = Val(ini, "check_url");
            string sid = Val(ini, "student_id");
            string op = Val(ini, "operator_type");
            string pw = Val(ini, "user_password");
            string ip = Val(ini, "fixed_ip");
            string interval = Val(ini, "check_interval");
            string retry = Val(ini, "retry_interval");
            string max = Val(ini, "max_retries");
            _ = long.TryParse(interval, out long ci);
            _ = long.TryParse(retry, out long ri);
            _ = long.TryParse(max, out long mr);
            return "{"
                + "\"auth_url\":\"" + EscQ(auth) + "\","
                + "\"check_url\":\"" + EscQ(check) + "\","
                + "\"student_id\":\"" + EscQ(sid) + "\","
                + "\"operator\":\"" + EscQ(op) + "\","
                + "\"password\":\"" + EscQ(pw) + "\","
                + "\"fixed_ip\":\"" + EscQ(ip) + "\","
                + "\"check_interval\":" + ci + ","
                + "\"retry_interval\":" + ri + ","
                + "\"max_retries\":" + mr + "}";
        }

        private void RefreshLogs_Click(object sender, RoutedEventArgs e) => RefreshLogs();

        private void InfoBar_Close(object sender, object e) => InfoBar.IsOpen = false;

        private void StatusInfoBar_Close(object sender, object e) => StatusInfoBar.IsOpen = false;

        private void MainWindow_Closed(object sender, WindowEventArgs args)
        {
            _tray.Dispose();
        }
    }
}
