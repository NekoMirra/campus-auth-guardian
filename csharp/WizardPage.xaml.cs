using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace CampusAuthGuardian
{
    public sealed partial class WizardPage : Page
    {
        private int _step = 1;
        private readonly MainWindow _main;

        public WizardPage(MainWindow main)
        {
            InitializeComponent();
            _main = main;
            ShowStep(1);
        }

        private void ShowStep(int n)
        {
            _step = n;
            StepTitle.Text = $"第 {n} 步，共 4 步";
            StepProgress.Value = n;
            Step1.Visibility = n == 1 ? Visibility.Visible : Visibility.Collapsed;
            Step2.Visibility = n == 2 ? Visibility.Visible : Visibility.Collapsed;
            Step3.Visibility = n == 3 ? Visibility.Visible : Visibility.Collapsed;
            Step4.Visibility = n == 4 ? Visibility.Visible : Visibility.Collapsed;
            BackBtn.IsEnabled = n > 1;
            NextBtn.Content = n == 4 ? "完成" : "下一步";
        }

        private async void WizTest_Click(object sender, RoutedEventArgs e)
        {
            WizTestBtn.IsEnabled = false;
            WizTestRing.IsActive = true;
            WizTestIcon.Glyph = "";
            WizTestText.Text = "检测中…";

            string url = WizAuthUrl.Text.Trim();
            string result = await Task.Run(() => Native.ProbeServer(url));
            try
            {
                using var doc = JsonDocument.Parse(result);
                bool reachable = doc.RootElement.TryGetProperty("reachable", out var r) && r.GetBoolean();
                long? latency = doc.RootElement.TryGetProperty("latency_ms", out var l) && l.ValueKind == JsonValueKind.Number ? l.GetInt64() : null;

                WizTestRing.IsActive = false;
                WizTestBtn.IsEnabled = true;
                if (reachable)
                {
                    WizTestIcon.Glyph = "\uE73E";
                    WizTestText.Text = $"服务器可达{(latency.HasValue ? $"（{latency}ms）" : "")}";
                }
                else
                {
                    WizTestIcon.Glyph = "\uE74D";
                    WizTestText.Text = "服务器不可达（可能需要在校园网内）";
                }
            }
            catch (JsonException)
            {
                WizTestRing.IsActive = false;
                WizTestBtn.IsEnabled = true;
                WizTestText.Text = "探测结果解析失败";
            }
        }

        private async void WizAuth_Click(object sender, RoutedEventArgs e)
        {
            // 认证前强校验：学号/密码必须非空
            if (string.IsNullOrWhiteSpace(WizStudentId.Text) || string.IsNullOrEmpty(WizPassword.Password))
            {
                WizAuthResult.Text = "✗ 学号或密码未填写，请点「上一步」补全";
                return;
            }
            // 先应用当前向导配置
            ApplyConfig();
            WizAuthBtn.IsEnabled = false;
            WizAuthRing.IsActive = true;
            WizAuthResult.Text = "认证中…";

            string result = await Task.Run(() => Native.AuthNow());
            WizAuthRing.IsActive = false;
            WizAuthBtn.IsEnabled = true;

            try
            {
                using var doc = JsonDocument.Parse(result);
                bool ok = doc.RootElement.TryGetProperty("ok", out var okEl) && okEl.GetBoolean();
                string status = doc.RootElement.TryGetProperty("status", out var sEl) ? sEl.GetString() ?? "" : "";
                string msg = doc.RootElement.TryGetProperty("msg", out var mEl) ? mEl.GetString() ?? "" : status;
                if (ok)
                {
                    WizAuthResult.Text = status == "already_online"
                        ? "✓ 本机已在线，无需重复认证"
                        : "✓ 认证成功，网络已连通";
                }
                else
                {
                    WizAuthResult.Text = $"✗ 认证失败：{msg}";
                }
            }
            catch (JsonException)
            {
                WizAuthResult.Text = result;
            }
        }

        private void ApplyConfig()
        {
            static string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
            string existing = Native.ConfigJson();
            string json = "{"
                + $"\"auth_url\":\"{Esc(WizAuthUrl.Text.Trim())}\","
                + $"\"student_id\":\"{Esc(WizStudentId.Text.Trim())}\","
                + $"\"password\":\"{Esc(WizPassword.Password)}\","
                + $"\"operator\":\"{((WizOperator.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "campus")}\""
                + "}";
            // merge: 保留其他字段
            Native.ConfigApply(json);
        }

        private void WizGuardian_Toggled(object sender, RoutedEventArgs e)
        {
            Native.GuardianSetEnabled(WizGuardian.IsOn ? 1 : 0);
        }

        private void Back_Click(object sender, RoutedEventArgs e)
        {
            if (_step > 1) ShowStep(_step - 1);
        }

        private void Next_Click(object sender, RoutedEventArgs e)
        {
            if (_step == 4)
            {
                // 完成：切回状态页
                _main.FinishWizard();
                return;
            }
            if (_step == 2 && (string.IsNullOrWhiteSpace(WizStudentId.Text) || string.IsNullOrEmpty(WizPassword.Password)))
            {
                WizStudentId.Header = "学号（必填）";
                return;
            }
            ShowStep(_step + 1);
        }
    }
}
