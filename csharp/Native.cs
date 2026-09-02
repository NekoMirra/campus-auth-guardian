using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace CampusAuthGuardian
{
    // Rust guardian-core FFI（对应 crates/guardian-core/src/ffi.rs）
    internal static class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct CStringOut
        {
            public IntPtr Ptr;
            public UIntPtr Len;
            public IntPtr Token;
        }

        [DllImport("guardian_core", EntryPoint = "guardian_init", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianInit([MarshalAs(UnmanagedType.LPUTF8Str)] string configPath, UIntPtr len);

        [DllImport("guardian_core", EntryPoint = "guardian_config_json", CallingConvention = CallingConvention.Cdecl)]
        private static extern CStringOut GuardianConfigJson();

        [DllImport("guardian_core", EntryPoint = "guardian_auth_now", CallingConvention = CallingConvention.Cdecl)]
        private static extern void GuardianAuthNowVoid();

        [DllImport("guardian_core", EntryPoint = "guardian_auth_now", CallingConvention = CallingConvention.Cdecl)]
        private static extern CStringOut GuardianAuthNowRet();

        [DllImport("guardian_core", EntryPoint = "guardian_recent_logs", CallingConvention = CallingConvention.Cdecl)]
        private static extern CStringOut GuardianRecentLogs();

        [DllImport("guardian_core", EntryPoint = "guardian_config_apply", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianConfigApply([MarshalAs(UnmanagedType.LPUTF8Str)] string json, UIntPtr len);

        [DllImport("guardian_core", EntryPoint = "guardian_set_enabled", CallingConvention = CallingConvention.Cdecl)]
        public static extern void GuardianSetEnabled(int on);

        [DllImport("guardian_core", EntryPoint = "guardian_state", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianState();

        [DllImport("guardian_core", EntryPoint = "guardian_poll_event", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GuardianPollEventNative(out IntPtr eventJson, out UIntPtr outLen, out IntPtr outToken);

        [DllImport("guardian_core", EntryPoint = "guardian_set_operator", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianSetOperator([MarshalAs(UnmanagedType.LPUTF8Str)] string op, UIntPtr len);

        [DllImport("guardian_core", EntryPoint = "guardian_set_autostart", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianSetAutostart(int on);

        [DllImport("guardian_core", EntryPoint = "guardian_is_autostart", CallingConvention = CallingConvention.Cdecl)]
        public static extern int GuardianIsAutostart();

        [DllImport("guardian_core", EntryPoint = "guardian_probe_server", CallingConvention = CallingConvention.Cdecl)]
        private static extern CStringOut GuardianProbeServer([MarshalAs(UnmanagedType.LPUTF8Str)] string url, UIntPtr len);

        public static string ProbeServer(string url) => TakeString(GuardianProbeServer(url, (UIntPtr)System.Text.Encoding.UTF8.GetByteCount(url)));

        [DllImport("guardian_core", EntryPoint = "guardian_string_free", CallingConvention = CallingConvention.Cdecl)]
        private static extern void GuardianStringFree(CStringOut out_);

        // 便捷封装
        public static int GuardianInit(string path) => GuardianInit(path, (UIntPtr)System.Text.Encoding.UTF8.GetByteCount(path));

        public static string TakeString(CStringOut o)
        {
            try
            {
                return o.Ptr == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(o.Ptr, (int)o.Len) ?? "";
            }
            finally
            {
                GuardianStringFree(o);
            }
        }

        public static string ConfigJson() => TakeString(GuardianConfigJson());
        public static void AuthNowFireAndForget() => GuardianAuthNowVoid();

        public static string AuthNow() => TakeString(GuardianAuthNowRet());
        public static string RecentLogs() => TakeString(GuardianRecentLogs());

        public static int ConfigApply(string json) => GuardianConfigApply(json, (UIntPtr)System.Text.Encoding.UTF8.GetByteCount(json));
        public static int SetOperator(string op) => GuardianSetOperator(op, (UIntPtr)System.Text.Encoding.UTF8.GetByteCount(op));

        private static readonly ConcurrentQueue<CStringOut> _pendingFree = new();

        public static bool TryPollEvent(out string json)
        {
            json = null;
            if (GuardianPollEventNative(out IntPtr ptr, out UIntPtr len, out IntPtr token) != 0) return false;
            try
            {
                json = ptr == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(ptr, (int)len) ?? "";
                return true;
            }
            finally
            {
                // by-value struct 跨 FFI 传参在 UI 线程回调中触发原生崩溃，
                // 改为延迟批量释放：每 64 条事件在独立线程统一 free
                _pendingFree.Enqueue(new CStringOut { Ptr = ptr, Len = len, Token = token });
                if (_pendingFree.Count >= 64)
                {
                    var batch = new List<CStringOut>();
                    while (_pendingFree.TryDequeue(out var item)) batch.Add(item);
                    Task.Run(() =>
                    {
                        foreach (var item in batch) GuardianStringFree(item);
                    });
                }
            }
        }
    }
}
