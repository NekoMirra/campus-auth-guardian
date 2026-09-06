//! 配置模型与 INI 读写。
//!
//! 兼容旧版 `user_account = ,0,学号@运营商` 格式；写入始终用新版字段。

use std::fmt;
use std::path::{Path, PathBuf};
use std::time::Duration;

/// 运营商类型。后缀 `@operator` 与之对应。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Operator {
    /// 校园网自有网络
    #[default]
    Campus,
    /// 中国移动
    Cmcc,
    /// 中国联通
    Unicom,
    /// 中国电信
    Telecom,
}

impl Operator {
    pub fn as_str(self) -> &'static str {
        match self {
            Operator::Campus => "campus",
            Operator::Cmcc => "cmcc",
            Operator::Unicom => "unicom",
            Operator::Telecom => "telecom",
        }
    }

    /// 全部运营商，供 UI 下拉框使用。
    pub const ALL: [Operator; 4] = [
        Operator::Campus,
        Operator::Cmcc,
        Operator::Unicom,
        Operator::Telecom,
    ];

    pub fn display(self) -> &'static str {
        match self {
            Operator::Campus => "校园网",
            Operator::Cmcc => "中国移动",
            Operator::Unicom => "中国联通",
            Operator::Telecom => "中国电信",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s.trim().to_ascii_lowercase().as_str() {
            "campus" => Some(Operator::Campus),
            "cmcc" => Some(Operator::Cmcc),
            "unicom" => Some(Operator::Unicom),
            "telecom" => Some(Operator::Telecom),
            _ => None,
        }
    }
}

impl fmt::Display for Operator {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// 应用配置。
#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub auth_url: String,
    pub check_url: String,
    pub check_interval: Duration,
    pub student_id: String,
    pub operator: Operator,
    pub password: String,
    /// 固定 IP；空 = 自动检测
    pub fixed_ip: Option<String>,
    pub guardian_enabled: bool,
    pub retry_interval: Duration,
    pub max_retries: u32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            auth_url: "http://10.10.102.50:801/eportal/portal/login".into(),
            check_url: "http://www.baidu.com".into(),
            check_interval: Duration::from_secs(30),
            student_id: String::new(),
            operator: Operator::default(),
            password: String::new(),
            fixed_ip: None,
            guardian_enabled: false,
            retry_interval: Duration::from_secs(10),
            max_retries: 3,
        }
    }
}

impl Config {
    /// 认证服务器基础地址（scheme+host+port），如 `http://10.10.102.50:801`。
    pub fn portal_base(&self) -> &str {
        self.auth_url
            .split("/eportal")
            .next()
            .unwrap_or(&self.auth_url)
            .trim_end_matches('/')
    }

    /// ePortal 登录页地址，用于抓取 PHPSESSID。
    pub fn login_page_url(&self) -> String {
        format!("{}/srun_portal_pc.php?ac_id=1&", self.portal_base())
    }
}

/// 简易 INI 区段解析结果。
#[derive(Debug, Default)]
struct Ini {
    network: Vec<(String, String)>,
    account: Vec<(String, String)>,
    guardian: Vec<(String, String)>,
}

fn parse_ini(text: &str) -> Ini {
    let mut ini = Ini::default();
    let mut section = "";
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(['#', ';']) {
            continue;
        }
        if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
            section = name.trim();
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            continue;
        };
        let (k, v) = (k.trim().to_string(), v.trim().to_string());
        match section {
            "network" => ini.network.push((k, v)),
            "account" => ini.account.push((k, v)),
            "guardian" => ini.guardian.push((k, v)),
            _ => {}
        }
    }
    ini
}

fn get<'a>(pairs: &'a [(String, String)], key: &str) -> Option<&'a str> {
    pairs
        .iter()
        .rev() // 后写的覆盖先写的
        .find(|(k, _)| k.eq_ignore_ascii_case(key))
        .map(|(_, v)| v.as_str())
}

fn get_u64(pairs: &[(String, String)], key: &str) -> Option<u64> {
    get(pairs, key)?.trim().parse().ok()
}

fn get_bool(pairs: &[(String, String)], key: &str) -> Option<bool> {
    match get(pairs, key)?.trim() {
        "1" | "true" | "yes" | "on" => Some(true),
        "0" | "false" | "no" | "off" => Some(false),
        _ => None,
    }
}

/// 从旧版 `user_account` 值（`,0,学号@运营商`）拆出学号与运营商。
fn split_legacy_account(v: &str) -> Option<(String, Operator)> {
    // 形如 ",0,24028116@unicom"
    let last = v.rsplit(',').next()?.trim();
    let (id, op) = last.split_once('@')?;
    let id = id.trim();
    if id.is_empty() {
        return None;
    }
    Some((id.to_string(), Operator::parse(op).unwrap_or_default()))
}

impl Config {
    /// 解析 INI 文本。缺失字段保留默认值；旧格式 `user_account` 自动兼容。
    pub fn parse(text: &str) -> Self {
        let ini = parse_ini(text);
        let mut cfg = Config::default();

        if let Some(v) = get(&ini.network, "auth_url") {
            if !v.is_empty() {
                cfg.auth_url = v.to_string();
            }
        }
        if let Some(v) = get(&ini.network, "check_url") {
            if !v.is_empty() {
                cfg.check_url = v.to_string();
            }
        }
        if let Some(v) = get_u64(&ini.network, "check_interval") {
            if (1..=3600).contains(&v) {
                cfg.check_interval = Duration::from_secs(v);
            }
        }

        if let Some(v) = get(&ini.account, "student_id") {
            cfg.student_id = v.trim().to_string();
        }
        if let Some(op) = get(&ini.account, "operator_type").and_then(Operator::parse) {
            cfg.operator = op;
        }
        if let Some(v) = get(&ini.account, "user_password") {
            cfg.password = v.to_string();
        }
        match get(&ini.account, "fixed_ip") {
            Some(v) if !v.trim().is_empty() => cfg.fixed_ip = Some(v.trim().to_string()),
            _ => cfg.fixed_ip = None,
        }
        // 旧格式兼容：student_id 缺失或 operator 走默认时尝试拆解
        if let Some((id, op)) = get(&ini.account, "user_account").and_then(split_legacy_account) {
            if cfg.student_id.is_empty() {
                cfg.student_id = id;
            }
            if cfg.operator == Operator::default() {
                cfg.operator = op;
            }
        }

        if let Some(v) = get_bool(&ini.guardian, "enabled") {
            cfg.guardian_enabled = v;
        }
        if let Some(v) = get_u64(&ini.guardian, "retry_interval") {
            if (1..=3600).contains(&v) {
                cfg.retry_interval = Duration::from_secs(v);
            }
        }
        if let Some(v) = get_u64(&ini.guardian, "max_retries") {
            if (1..=100).contains(&v) {
                cfg.max_retries = v as u32;
            }
        }
        cfg
    }

    /// 序列化为新版 INI（UTF-8，含注释）。
    pub fn to_ini(&self) -> String {
        format!(
            "# Campus Auth Guardian 配置文件 (UTF-8)\n\
             \n\
             [network]\n\
             auth_url = {auth}\n\
             check_url = {check}\n\
             check_interval = {interval}\n\
             \n\
             [account]\n\
             student_id = {id}\n\
             operator_type = {op}\n\
             user_password = {pw}\n\
             fixed_ip = {ip}\n\
             \n\
             [guardian]\n\
             enabled = {en}\n\
             retry_interval = {retry}\n\
             max_retries = {max}\n",
            auth = self.auth_url,
            check = self.check_url,
            interval = self.check_interval.as_secs(),
            id = self.student_id,
            op = self.operator.as_str(),
            pw = self.password,
            ip = self.fixed_ip.as_deref().unwrap_or(""),
            en = if self.guardian_enabled { 1 } else { 0 },
            retry = self.retry_interval.as_secs(),
            max = self.max_retries,
        )
    }

    /// 读取配置文件；不存在时写入默认模板。
    pub fn load_or_create(path: &Path) -> std::io::Result<Config> {
        match std::fs::read_to_string(path) {
            Ok(text) => Ok(Config::parse(&text)),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                let cfg = Config::default();
                if let Some(dir) = path.parent() {
                    std::fs::create_dir_all(dir)?;
                }
                std::fs::write(path, cfg.to_ini())?;
                Ok(cfg)
            }
            Err(e) => Err(e),
        }
    }

    /// 原子写入配置文件（先写临时文件再 rename）。
    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        let tmp = path.with_extension("ini.tmp");
        std::fs::write(&tmp, self.to_ini())?;
        std::fs::rename(&tmp, path)?;
        Ok(())
    }

    /// 供 UI 持久化的默认配置路径（exe 同目录优先，失败回退 %APPDATA%）。
    pub fn default_path() -> PathBuf {
        if let Ok(exe) = std::env::current_exe() {
            if let Some(dir) = exe.parent() {
                let p = dir.join("config.ini");
                // 无副作用探测：已存在直接用；否则父目录可写即可（不写探针文件）
                if p.exists() {
                    return p;
                }
                if let Ok(meta) = std::fs::metadata(dir) {
                    if !meta.permissions().readonly() {
                        return p;
                    }
                }
            }
        }
        let appdata = std::env::var("APPDATA").unwrap_or_else(|_| ".".into());
        PathBuf::from(appdata).join("CampusAuthGuardian").join("config.ini")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_standard_ini() {
        let cfg = Config::parse(
            "[network]\nauth_url = http://1.2.3.4:801/eportal/portal/login\ncheck_interval = 15\n\
             [account]\nstudent_id = 24028116\noperator_type = unicom\nuser_password = pw\nfixed_ip = 10.0.0.1\n\
             [guardian]\nenabled = 1\nretry_interval = 5\nmax_retries = 7\n",
        );
        assert_eq!(cfg.auth_url, "http://1.2.3.4:801/eportal/portal/login");
        assert_eq!(cfg.check_interval, Duration::from_secs(15));
        assert_eq!(cfg.student_id, "24028116");
        assert_eq!(cfg.operator, Operator::Unicom);
        assert_eq!(cfg.password, "pw");
        assert_eq!(cfg.fixed_ip.as_deref(), Some("10.0.0.1"));
        assert!(cfg.guardian_enabled);
        assert_eq!(cfg.retry_interval, Duration::from_secs(5));
        assert_eq!(cfg.max_retries, 7);
    }

    #[test]
    fn parses_legacy_user_account() {
        let cfg = Config::parse("[account]\nuser_account = ,0,24028116@unicom\nuser_password = pw\n");
        assert_eq!(cfg.student_id, "24028116");
        assert_eq!(cfg.operator, Operator::Unicom);
        assert_eq!(cfg.password, "pw");
    }

    #[test]
    fn defaults_when_empty() {
        let cfg = Config::parse("");
        assert_eq!(cfg, Config::default());
    }

    #[test]
    fn interval_bounds_clamped() {
        let cfg = Config::parse("[network]\ncheck_interval = 99999\n[guardian]\nretry_interval = 0\nmax_retries = 9999\n");
        assert_eq!(cfg.check_interval, Duration::from_secs(30)); // 默认
        assert_eq!(cfg.retry_interval, Duration::from_secs(10)); // 默认
        assert_eq!(cfg.max_retries, 3); // 默认
    }

    #[test]
    fn portal_base_strips_path() {
        let cfg = Config::parse("[network]\nauth_url = http://10.10.102.50:801/eportal/portal/login\n");
        assert_eq!(cfg.portal_base(), "http://10.10.102.50:801");
    }

    #[test]
    fn roundtrip_ini() {
        let mut cfg = Config::default();
        cfg.student_id = "abc".into();
        cfg.operator = Operator::Telecom;
        cfg.guardian_enabled = true;
        let text = cfg.to_ini();
        assert_eq!(Config::parse(&text), cfg);
    }

    #[test]
    fn last_value_wins() {
        let cfg = Config::parse("[account]\nstudent_id = a\nstudent_id = b\n");
        assert_eq!(cfg.student_id, "b");
    }

    #[test]
    fn edge_empty_and_comments() {
        let cfg = Config::parse("\n;; 分号注释\n# 井号注释\n   \n[account]\n");
        assert_eq!(cfg, Config::default());
    }

    #[test]
    fn edge_crlf_line_endings() {
        let cfg = Config::parse("[account]\r\nstudent_id = abc\r\nuser_password = pw\r\n");
        assert_eq!(cfg.student_id, "abc");
        assert_eq!(cfg.password, "pw");
    }

    #[test]
    fn edge_no_space_around_equals() {
        let cfg = Config::parse("[account]\nstudent_id=nospace\nuser_password=pw1\n");
        assert_eq!(cfg.student_id, "nospace");
        assert_eq!(cfg.password, "pw1");
    }

    #[test]
    fn edge_value_with_equals() {
        let cfg = Config::parse("[account]\nstudent_id = a=b=c\n");
        assert_eq!(cfg.student_id, "a=b=c");
    }

    #[test]
    fn edge_interval_bounds() {
        // 下边界
        let lo = Config::parse("[network]\ncheck_interval = 1\n[guardian]\nretry_interval = 1\nmax_retries = 1\n");
        assert_eq!(lo.check_interval, Duration::from_secs(1));
        assert_eq!(lo.retry_interval, Duration::from_secs(1));
        assert_eq!(lo.max_retries, 1);
        // 上边界
        let hi = Config::parse("[network]\ncheck_interval = 3600\n[guardian]\nretry_interval = 3600\nmax_retries = 100\n");
        assert_eq!(hi.check_interval, Duration::from_secs(3600));
        assert_eq!(hi.retry_interval, Duration::from_secs(3600));
        assert_eq!(hi.max_retries, 100);
        // 非法：负数/非数字/0
        let bad = Config::parse("[network]\ncheck_interval = -5\n[guardian]\nmax_retries = abc\nretry_interval = 0\n");
        assert_eq!(bad.check_interval, Duration::from_secs(30));
        assert_eq!(bad.max_retries, 3);
        assert_eq!(bad.retry_interval, Duration::from_secs(10));
    }

    #[test]
    fn edge_unicode_values() {
        let cfg = Config::parse("[account]\nstudent_id = 学号123🎯\n");
        assert_eq!(cfg.student_id, "学号123🎯");
    }
}
