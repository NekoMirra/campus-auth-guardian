using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace CampusAuthGuardian
{
    public sealed partial class AboutPage : Page
    {
        private const string RepoApi = "https://api.github.com/repos/NekoMirra/campus-auth-guardian/releases/latest";
        private const string CurrentVersion = "2.2.0";

        public AboutPage()
        {
            InitializeComponent();
            VersionText.Text = $"v{CurrentVersion}";
            ArchText.Text = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString();
        }

        private async void CheckUpdate_Click(object sender, RoutedEventArgs e)
        {
            CheckUpdateBtn.IsEnabled = false;
            UpdateRing.IsActive = true;
            UpdateRing.Visibility = Visibility.Visible;
            UpdateInfoBar.IsOpen = false;

            string? latestTag = null;
            string? downloadUrl = null;
            string? error = null;

            try
            {
                using var http = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(10) };
                // GitHub API 要求 UA
                http.DefaultRequestHeaders.UserAgent.ParseAdd("CampusAuthGuardian");
                var resp = await http.GetStringAsync(RepoApi);
                using var doc = JsonDocument.Parse(resp);
                latestTag = doc.RootElement.TryGetProperty("tag_name", out var tag) ? tag.GetString() : null;
                if (doc.RootElement.TryGetProperty("assets", out var assets))
                {
                    foreach (var a in assets.EnumerateArray())
                    {
                        var arch = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture;
                        string want = arch == System.Runtime.InteropServices.Architecture.Arm64 ? "ARM64" : "x64";
                        var name = a.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "";
                        if (name.Contains(want) && name.EndsWith(".zip"))
                        {
                            downloadUrl = a.TryGetProperty("browser_download_url", out var u) ? u.GetString() : null;
                            break;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                error = ex.Message;
            }

            UpdateRing.IsActive = false;
            UpdateRing.Visibility = Visibility.Collapsed;
            CheckUpdateBtn.IsEnabled = true;

            if (error != null)
            {
                ShowUpdate(InfoBarSeverity.Error, $"检查失败：{Truncate(error, 120)}", null);
                return;
            }

            var latest = (latestTag ?? "").TrimStart('v');
            var current = CurrentVersion.TrimStart('v');
            var isNewer = CompareVersions(latest, current) > 0;

            if (isNewer)
            {
                ShowUpdate(InfoBarSeverity.Warning,
                    $"发现新版本 {latestTag}（当前 v{current}）",
                    downloadUrl);
            }
            else
            {
                ShowUpdate(InfoBarSeverity.Success,
                    $"已是最新版本（v{current}）",
                    null);
            }
        }

        private static int CompareVersions(string a, string b)
        {
            var pa = a.Split('.', StringSplitOptions.RemoveEmptyEntries);
            var pb = b.Split('.', StringSplitOptions.RemoveEmptyEntries);
            for (int i = 0; i < Math.Max(pa.Length, pb.Length); i++)
            {
                int va = i < pa.Length && int.TryParse(pa[i], out var na) ? na : 0;
                int vb = i < pb.Length && int.TryParse(pb[i], out var nb) ? nb : 0;
                if (va != vb) return va.CompareTo(vb);
            }
            return 0;
        }

        private void ShowUpdate(InfoBarSeverity severity, string message, string? url)
        {
            UpdateInfoBar.Severity = severity;
            UpdateInfoBar.Message = message;
            UpdateInfoBar.IsOpen = true;
            UpdateInfoBar.Tag = url;
            UpdateInfoBar.ActionButton ??= new HyperlinkButton
            {
                Content = "前往下载",
            };
            if (UpdateInfoBar.ActionButton is HyperlinkButton hb)
            {
                hb.NavigateUri = string.IsNullOrEmpty(url) ? null : new Uri(url);
                hb.Visibility = string.IsNullOrEmpty(url) ? Visibility.Collapsed : Visibility.Visible;
            }
        }

        private void UpdateInfoBar_Close(InfoBar sender, object args)
        {
            sender.IsOpen = false;
        }

        private static string Truncate(string s, int n) => s.Length <= n ? s : s[..n] + "…";
    }
}
