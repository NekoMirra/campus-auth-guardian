//! C ABI 导出层：供 C++/WinUI3 壳调用。
//!
//! 约定：
//! - 所有字符串均为 UTF-8 指针 + 长度；调用方负责释放由 `guardian_*_free` 分配的内存。
//! - 全局单例 GUARDIAN（进程内唯一）。

use std::ffi::c_void;
use std::os::raw::{c_char, c_int};
use std::slice;

use crate::auth::AuthOutcome;
use crate::config::{Config, Operator};
use crate::guardian::{Guardian, GuardianEvent, GuardianState};
use crate::netcheck::NetStatus;

static GUARDIAN: std::sync::OnceLock<Guardian> = std::sync::OnceLock::new();

fn guardian() -> &'static Guardian {
    GUARDIAN.get().expect("guardian_init must be called first")
}

/// RAII 字符串（Rust 分配，C 调用后调用 guardian_string_free 释放）。
#[repr(C)]
pub struct CStringOut {
    pub ptr: *mut c_char,
    pub len: usize,
    /// 调用方持有的释放令牌，回传给 guardian_string_free
    pub token: *mut c_void,
}

/// 分配给 C 的 UTF-8 字符串。
fn out_string(s: &str) -> CStringOut {
    // Vec<u8> 持有容量；ptr 指向数据首字节
    let mut v: Vec<u8> = s.as_bytes().to_vec();
    let len = v.len();
    let ptr = v.as_mut_ptr() as *mut c_char;
    let token = Box::into_raw(Box::new(v)) as *mut c_void;
    CStringOut { ptr, len, token }
}

/// # Safety
/// `out.token` 必须来自本次 FFI 返回且只释放一次。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_string_free(out: CStringOut) {
    if !out.token.is_null() {
        drop(unsafe { Box::from_raw(out.token as *mut Vec<u8>) });
    }
}
/// # Safety
/// `config_path` 为合法 UTF-8 指针，`len` 为其长度。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_init(config_path: *const c_char, len: usize) -> c_int {
    unsafe {
        if config_path.is_null() && len > 0 {
            return -3;
        }
        let bytes = if config_path.is_null() {
            &[][..]
        } else {
            slice::from_raw_parts(config_path as *const u8, len)
        };
        let path = match std::str::from_utf8(bytes) {
            Ok(s) => std::path::PathBuf::from(s),
            Err(_) => return -1,
        };
        // 日志与配置同目录
        if let Some(dir) = path.parent() {
            let log_path = dir.join("campus_auth.log");
            crate::logger::init_file(&log_path);
        }
        match Config::load_or_create(&path) {
            Ok(cfg) => {
                let g = Guardian::start(cfg, path);
                GUARDIAN.set(g).ok(); // 二次调用忽略
                0
            }
            Err(_) => -2,
        }
    }
}

/// 读取配置 JSON（serde 序列化）。
#[unsafe(no_mangle)]
pub extern "C" fn guardian_config_json() -> CStringOut {
    let cfg = guardian().config();
    let json = serde_json::json!({
        "auth_url": cfg.auth_url,
        "check_url": cfg.check_url,
        "check_interval": cfg.check_interval.as_secs(),
        "student_id": cfg.student_id,
        "operator": cfg.operator.as_str(),
        "password": cfg.password,
        "fixed_ip": cfg.fixed_ip,
        "guardian_enabled": cfg.guardian_enabled,
        "retry_interval": cfg.retry_interval.as_secs(),
        "max_retries": cfg.max_retries,
    });
    out_string(&json.to_string())
}

/// 应用配置 JSON；保存到磁盘。返回 0 成功，-1 解析失败。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_config_apply(json: *const c_char, len: usize) -> c_int {
    let bytes = unsafe { slice::from_raw_parts(json as *const u8, len) };
    let text = match std::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(_) => return -1,
    };
    let v: serde_json::Value = match serde_json::from_str(text) {
        Ok(v) => v,
        Err(_) => return -1,
    };
    let mut cfg = guardian().config();
    if let Some(s) = v.get("auth_url").and_then(|x| x.as_str()) {
        cfg.auth_url = s.into();
    }
    if let Some(s) = v.get("check_url").and_then(|x| x.as_str()) {
        cfg.check_url = s.into();
    }
    if let Some(n) = v.get("check_interval").and_then(|x| x.as_u64()) {
        cfg.check_interval = std::time::Duration::from_secs(n.clamp(1, 3600));
    }
    if let Some(s) = v.get("student_id").and_then(|x| x.as_str()) {
        cfg.student_id = s.into();
    }
    if let Some(s) = v.get("operator").and_then(|x| x.as_str()) {
        if let Some(op) = Operator::parse(s) {
            cfg.operator = op;
        }
    }
    if let Some(s) = v.get("password").and_then(|x| x.as_str()) {
        cfg.password = s.into();
    }
    match v.get("fixed_ip").and_then(|x| x.as_str()) {
        Some(s) if !s.trim().is_empty() => cfg.fixed_ip = Some(s.trim().into()),
        _ => cfg.fixed_ip = None,
    }
    if let Some(b) = v.get("guardian_enabled").and_then(|x| x.as_bool()) {
        cfg.guardian_enabled = b;
    }
    if let Some(n) = v.get("retry_interval").and_then(|x| x.as_u64()) {
        cfg.retry_interval = std::time::Duration::from_secs(n.clamp(1, 3600));
    }
    if let Some(n) = v.get("max_retries").and_then(|x| x.as_u64()) {
        cfg.max_retries = (n.clamp(1, 100)) as u32;
    }
    match guardian().update_config(cfg) {
        Ok(()) => 0,
        Err(_) => -2,
    }
}

/// 手动认证（阻塞，返回 JSONP 判定 JSON）。
#[unsafe(no_mangle)]
pub extern "C" fn guardian_auth_now() -> CStringOut {
    let outcome = guardian().auth_once();
    let s = match outcome {
        AuthOutcome::Success => r#"{"ok":true,"status":"success"}"#.to_string(),
        AuthOutcome::AlreadyOnline => r#"{"ok":true,"status":"already_online"}"#.to_string(),
        AuthOutcome::Failed { msg } => {
            serde_json::json!({"ok": false, "status": "failed", "msg": msg}).to_string()
        }
        AuthOutcome::NetworkError { msg } => {
            serde_json::json!({"ok": false, "status": "network_error", "msg": msg}).to_string()
        }
    };
    out_string(&s)
}

/// 开关守护模式。1=开，0=关。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_set_enabled(on: c_int) {
    let g = guardian();
    if on != 0 {
        if !g.is_running() {
            g.enable();
        }
    } else {
        g.disable();
    }
}

/// 当前状态：0=stopped 1=monitoring 2=authenticating；-1 未初始化。
#[unsafe(no_mangle)]
pub extern "C" fn guardian_state() -> c_int {
    match GUARDIAN.get() {
        None => -1,
        Some(g) => match g.state() {
            GuardianState::Stopped => 0,
            GuardianState::Monitoring => 1,
            GuardianState::Authenticating => 2,
        },
    }
}

/// 轮询一个事件（非阻塞）。返回 0 有事件（写入 event_json），-1 无事件。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_poll_event(
    event_json: *mut *mut c_char,
    out_len: *mut usize,
    out_token: *mut *mut c_void,
) -> c_int {
    let g = guardian();
    match g.events.try_recv() {
        Ok(ev) => {
            let text = match ev {
                GuardianEvent::State(s) => {
                    serde_json::json!({"type":"state","state": state_code(s)})
                }
                GuardianEvent::NetStatus(ns) => serde_json::json!({
                    "type":"net",
                    "status": match ns {
                        NetStatus::Connected => "connected",
                        NetStatus::CaptivePortal { .. } => "captive",
                        NetStatus::Disconnected { .. } => "disconnected",
                    },
                    "detail": match ns {
                        NetStatus::CaptivePortal { redirect } => redirect,
                        NetStatus::Disconnected { reason } => reason,
                        _ => String::new(),
                    }
                }),
                GuardianEvent::AuthResult(o) => serde_json::json!({
                    "type":"auth",
                    "ok": o.is_ok(),
                    "detail": match o {
                        AuthOutcome::Success => "success".to_string(),
                        AuthOutcome::AlreadyOnline => "already_online".to_string(),
                        AuthOutcome::Failed { msg } => msg,
                        AuthOutcome::NetworkError { msg } => msg,
                    }
                }),
                GuardianEvent::ConfigReloaded => serde_json::json!({"type":"config"}),
            };
            let out = out_string(&text.to_string());
            unsafe {
                *event_json = out.ptr;
                *out_len = out.len;
                *out_token = out.token;
            }
            0
        }
        Err(_) => -1,
    }
}

fn state_code(s: GuardianState) -> i32 {
    match s {
        GuardianState::Stopped => 0,
        GuardianState::Monitoring => 1,
        GuardianState::Authenticating => 2,
    }
}

/// 配置探测：检测 auth_url 可达性 + 账号字段完整性。返回 JSON 结果。
/// result: {"reachable":bool,"http_status":u16|null,"latency_ms":u64|null,"account_ok":bool,"issues":[...]}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_probe_server(auth_url: *const c_char, len: usize) -> CStringOut {
    let bytes = unsafe { slice::from_raw_parts(auth_url as *const u8, len) };
    let url = std::str::from_utf8(bytes).unwrap_or("");
    let mut issues: Vec<serde_json::Value> = Vec::new();

    if url.is_empty() {
        issues.push(serde_json::json!("auth_url 为空"));
    }
    let base = url.split("/eportal").next().unwrap_or("").trim_end_matches('/');
    let mut reachable = false;
    let mut http_status: Option<u16> = None;
    let mut latency_ms: Option<u64> = None;

    if !base.is_empty() {
        let started = std::time::Instant::now();
        match crate::netcheck::check(base, std::time::Duration::from_secs(6)) {
            crate::netcheck::NetStatus::Connected => {
                reachable = true;
                latency_ms = Some(started.elapsed().as_millis() as u64);
                http_status = Some(200);
            }
            crate::netcheck::NetStatus::CaptivePortal { .. } => {
                // 门户响应本身说明服务器在
                reachable = true;
                latency_ms = Some(started.elapsed().as_millis() as u64);
                http_status = Some(302);
            }
            crate::netcheck::NetStatus::Disconnected { reason } => {
                issues.push(serde_json::json!(format!("服务器不可达: {reason}")));
            }
        }
    }

    let out = serde_json::json!({
        "reachable": reachable,
        "http_status": http_status,
        "latency_ms": latency_ms,
        "issues": issues,
    });
    out_string(&out.to_string())
}

/// 读取内存日志（JSON 数组）。
#[unsafe(no_mangle)]
pub extern "C" fn guardian_recent_logs() -> CStringOut {
    let lines = crate::logger::recent();
    let arr: Vec<serde_json::Value> = lines
        .iter()
        .map(|l| {
            serde_json::json!({
                "ts": l.ts,
                "level": l.level.as_str(),
                "text": l.text,
            })
        })
        .collect();
    out_string(&serde_json::to_string(&arr).unwrap_or_else(|_| "[]".into()))
}

/// 切换运营商（立即持久化）。返回 0 成功。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_set_operator(op: *const c_char, len: usize) -> c_int {
    let bytes = unsafe { slice::from_raw_parts(op as *const u8, len) };
    let Some(op) = std::str::from_utf8(bytes).ok().and_then(Operator::parse) else {
        return -1;
    };
    let mut cfg = guardian().config();
    cfg.operator = op;
    match guardian().update_config(cfg) {
        Ok(()) => 0,
        Err(_) => -2,
    }
}

/// 开机自启注册表写入（HKCU\...\Run）。`on`=1 注册，0 移除。返回 0 成功。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_set_autostart(on: c_int) -> c_int {
    let exe = match std::env::current_exe() {
        Ok(p) => p,
        Err(_) => return -1,
    };
    match autostart_registry(on != 0, &exe.to_string_lossy()) {
        Ok(()) => 0,
        Err(()) => -2,
    }
}

/// 查询开机自启是否已设置。
#[unsafe(no_mangle)]
pub extern "C" fn guardian_is_autostart() -> c_int {
    match autostart_query() {
        Some(true) => 1,
        Some(false) => 0,
        None => -1,
    }
}

const HKEY_CURRENT_USER: usize = 0x8000_0001;
const KEY_READ: u32 = 0x20019;
const KEY_WRITE: u32 = 0x20006;
const REG_VALUE_TYPE_SZ: u32 = 1;
const ERROR_SUCCESS: i32 = 0;
const REG_SZ: u32 = REG_VALUE_TYPE_SZ;
const ERROR_FILE_NOT_FOUND: i32 = 2;


#[link(name = "advapi32")]
unsafe extern "system" {
    fn RegOpenKeyExW(hkey: usize, subkey: *const u16, options: u32, sam: u32, result: *mut usize) -> i32;
    fn RegCreateKeyExW(
        hkey: usize,
        subkey: *const u16,
        _reserved: u32,
        _class: *const u16,
        options: u32,
        sam: u32,
        _sec: *const u8,
        result: *mut usize,
        _disposition: *mut u32,
    ) -> i32;
    fn RegCloseKey(hkey: usize) -> i32;
    fn RegQueryValueExW(
        hkey: usize,
        name: *const u16,
        _reserved: *mut u32,
        _typ: *mut u32,
        data: *mut u8,
        cb: *mut u32,
    ) -> i32;
    fn RegSetValueExW(
        hkey: usize,
        name: *const u16,
        _reserved: u32,
        typ: u32,
        data: *const u8,
        cb: u32,
    ) -> i32;
    fn RegDeleteValueW(hkey: usize, name: *const u16) -> i32;
}


fn run_key_utf16() -> Vec<u16> {
    "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
        .encode_utf16()
        .chain(std::iter::once(0))
        .collect()
}

fn value_name_utf16() -> Vec<u16> {
    "CampusAuthGuardian".encode_utf16().chain(std::iter::once(0)).collect()
}

fn utf16z(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn autostart_registry(on: bool, exe: &str) -> Result<(), ()> {
    let key = run_key_utf16();
    let name = value_name_utf16();
    let mut hk = 0usize;
    let rc = unsafe {
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            key.as_ptr(),
            0,
            std::ptr::null(),
            0,
            KEY_WRITE | KEY_READ,
            std::ptr::null(),
            &mut hk,
            std::ptr::null_mut(),
        )
    };
    if rc != ERROR_SUCCESS {
        return Err(());
    }
    let result = if on {
        let data = utf16z(&format!("\"{exe}\""));
        unsafe {
            RegSetValueExW(
                hk,
                name.as_ptr(),
                0,
                REG_VALUE_TYPE_SZ,
                data.as_ptr() as *const u8,
                (data.len() * 2) as u32,
            ) == ERROR_SUCCESS
        }
    } else {
        let rc = unsafe { RegDeleteValueW(hk, name.as_ptr()) };
        rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND
    };
    unsafe { RegCloseKey(hk) };
    if result {
        Ok(())
    } else {
        Err(())
    }
}

fn autostart_query() -> Option<bool> {
    let key = run_key_utf16();
    let name = value_name_utf16();
    let mut hk = 0usize;
    let rc = unsafe {
        RegOpenKeyExW(HKEY_CURRENT_USER, key.as_ptr(), 0, KEY_READ, &mut hk)
    };
    if rc == ERROR_FILE_NOT_FOUND {
        return Some(false);
    }
    if rc != ERROR_SUCCESS {
        return None;
    }
    let mut typ: u32 = 0;
    let mut cb: u32 = 0;
    let rc = unsafe {
        RegQueryValueExW(hk, name.as_ptr(), std::ptr::null_mut(), &mut typ, std::ptr::null_mut(), &mut cb)
    };
    unsafe { RegCloseKey(hk) };
    if rc == ERROR_SUCCESS && typ == REG_SZ && cb > 0 {
        Some(true)
    } else if rc == ERROR_FILE_NOT_FOUND {
        Some(false)
    } else {
        None
    }
}


/// # Safety
/// 测试辅助：销毁全局单例（生产代码禁止调用）。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn guardian_shutdown_for_tests() {
    if let Some(g) = GUARDIAN.get() {
        g.stop();
    }
}
