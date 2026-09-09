using System.Reflection;

namespace CampusAuthGuardian
{
    /// <summary>
    /// 应用当前版本。CI 在 tag 构建时经 <c>/p:InformationalVersion=vX.Y.Z</c> 注入；
    /// 本地构建回退到程序集版本，拿不到则报 dev（检查更新时视为低于任何正式版）。
    /// </summary>
    internal static class AppVersion
    {
        public static string Current
        {
            get
            {
                var asm = Assembly.GetEntryAssembly() ?? Assembly.GetExecutingAssembly();
                var cleaned = Clean(asm.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion);
                if (cleaned != null) return cleaned;
                var ver = asm.GetName().Version;
                if (ver != null && (ver.Major > 0 || ver.Minor > 0 || ver.Build > 0)) return ver.ToString();
                return "0.0.0-dev";
            }
        }

        private static string? Clean(string? raw)
        {
            if (string.IsNullOrWhiteSpace(raw)) return null;
            var v = raw.Trim().TrimStart('v', 'V');
            foreach (var sep in new[] { '+', '-', ' ' })
            {
                int i = v.IndexOf(sep);
                if (i >= 0) v = v[..i];
            }
            return System.Version.TryParse(v, out _) ? v : null;
        }
    }
}
