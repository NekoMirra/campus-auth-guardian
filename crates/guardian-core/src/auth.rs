//! ePortal JSONP 认证协议实现。
//!
//! 协议要点（从现网抓包/日志还原）：
//! - 请求：GET `{base}/eportal/portal/login?callback=dr1005&login_method=1&...`
//! - `user_account` 需 URL 编码：`,0,{学号}@{运营商}` → `%2C0%2C...%40unicom`
//! - 响应：JSONP `dr1005({"result":1,"msg":"...","ret_code":0})`
//! - `result=1` 成功；`result=0` 且 `ret_code=2` 已在线（视为成功）；其余失败

use serde_json::Value;

use crate::config::Config;
use crate::ipdetect;
use crate::{log_info, log_warn};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AuthOutcome {
    Success,
    AlreadyOnline,
    Failed { msg: String },
    NetworkError { msg: String },
}

impl AuthOutcome {
    pub fn is_ok(&self) -> bool {
        matches!(self, AuthOutcome::Success | AuthOutcome::AlreadyOnline)
    }
}

/// URL 百分号编码（RFC 3986 非保留字符外的全部转义）。
pub fn urlencode(s: &str) -> String {
    let mut out = String::with_capacity(s.len() * 3);
    for &b in s.as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char)
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

/// 构造认证 URL。`ac` 为 (wlan_ac_ip, wlan_ac_name)，从 captive portal 重定向提取；未知传空。
pub fn build_login_url(cfg: &Config, ip: &str, callback: &str, ac: Option<(&str, &str)>) -> String {
    let account = format!(",0,{}@{}", cfg.student_id, cfg.operator.as_str());
    let (ac_ip, ac_name) = ac.unwrap_or(("", ""));
    format!(
        "{}?callback={cb}&login_method=1&user_account={acc}&user_password={pw}\
         &wlan_user_ip={ip}&wlan_user_ipv6=&wlan_user_mac=000000000000\
         &wlan_ac_ip={acip}&wlan_ac_name={acname}&jsVersion=4.1.3&terminal_type=1&lang=zh-cn&v=3015&lang=zh",
        cfg.auth_url,
        cb = urlencode(callback),
        acc = urlencode(&account),
        pw = urlencode(&cfg.password),
        ip = urlencode(ip),
        acip = urlencode(ac_ip),
        acname = urlencode(ac_name),
    )
}

/// 解析 JSONP `dr1005({...})` 提取 result/ret_code/msg。
fn parse_jsonp(text: &str) -> Option<(i64, Option<i64>, String)> {
    let start = text.find('(')?;
    let end = text.rfind(')')?;
    if start >= end {
        return None;
    }
    let v: Value = serde_json::from_str(&text[start + 1..end]).ok()?;
    let result = v.get("result")?.as_i64()?;
    let ret_code = v.get("ret_code").and_then(|r| r.as_i64());
    let msg = v
        .get("msg")
        .and_then(|m| m.as_str())
        .unwrap_or("")
        .to_string();
    Some((result, ret_code, msg))
}

/// 执行一次认证：收集全部候选 IP（fixed_ip 优先 + 本机各网卡按评分排序），
/// 逐一尝试，任一成功/已在线立即返回；全部失败返回最后一个失败结果。
/// IP 会漂移（DHCP 换段、多网卡），不绑定单一 IP。
pub fn authenticate(cfg: &Config) -> AuthOutcome {
    let mut candidates: Vec<String> = Vec::new();

    // 1) 固定 IP 最优先（用户显式指定）
    if let Some(f) = cfg.fixed_ip.as_deref() {
        if !f.trim().is_empty() {
            candidates.push(f.trim().to_string());
        }
    }
    // 2) 本机所有 IPv4（评分排序：10.x 优先）
    for a in crate::ipdetect::list_adapters() {
        if !candidates.contains(&a.ip) {
            candidates.push(a.ip);
        }
    }
    // 评分排序（除第一个 fixed 外）
    if candidates.len() > 1 {
        let mut rest = candidates.split_off(1);
        rest.sort_by_key(|ip| std::cmp::Reverse(ip_score(ip)));
        candidates.extend(rest);
    }
    // 去重相邻
    candidates.dedup();

    if candidates.is_empty() {
        return AuthOutcome::NetworkError { msg: "未检测到可用本机 IP".into() };
    }
    log_info!("认证候选 IP: {:?}", candidates);

    let mut last: Option<AuthOutcome> = None;
    for (i, ip) in candidates.iter().enumerate() {
        let outcome = authenticate_with_ip(cfg, ip, None);
        let ok = outcome.is_ok();
        log_info!("候选 {}/{} IP {ip} 结果: {}", i + 1, candidates.len(),
            match &outcome {
                AuthOutcome::Success => "成功".to_string(),
                AuthOutcome::AlreadyOnline => "已在线".to_string(),
                AuthOutcome::Failed { msg } => format!("失败({msg})"),
                AuthOutcome::NetworkError { msg } => format!("网络错误({msg})"),
            });
        if ok {
            return outcome; // 成功/已在线立即返回
        }
        last = Some(outcome);
    }
    last.unwrap_or_else(|| AuthOutcome::NetworkError { msg: "全部候选 IP 认证失败".into() })
}

/// 带门户 AC 参数的认证（captive portal 检测到后调用）。
/// `ac` = (wlanacip, wlanacname, portal_user_ip)；portal_user_ip 非空时插到候选最前。
pub fn authenticate_with_ac(cfg: &Config, ac: Option<(&str, &str, &str)>) -> AuthOutcome {
    // portal 报告的 user_ip 是 AC 认定的会话 IP，必须最优先使用
    if let Some((_, _, portal_ip)) = ac {
        if !portal_ip.is_empty() {
            let mut cfg2 = cfg.clone();
            cfg2.fixed_ip = Some(portal_ip.to_string());
            return authenticate(&cfg2);
        }
    }
    authenticate(cfg)
}

/// IP 候选评分：10.x 校园网最高，其次 192.168 非网关，172.16-31 最低。
pub fn ip_score(ip: &str) -> u8 {
    let octets: Vec<u32> = ip.split('.').filter_map(|o| o.parse().ok()).collect();
    if octets.len() != 4 {
        return 0;
    }
    let (a, b, c) = (octets[0], octets[1], octets[2]);
    if a == 10 {
        5
    } else if a == 192 && b == 168 && c != 1 {
        4
    } else if a == 192 && b == 168 {
        3
    } else if a == 172 && (16..=31).contains(&b) {
        2
    } else {
        1
    }
}

fn authenticate_with_ip(cfg: &Config, ip: &str, ac: Option<(&str, &str)>) -> AuthOutcome {
    // 与旧版一致：先 GET 登录页（尝试取 PHPSESSID；未取到也继续）
    let agent = match ureq::AgentBuilder::new()
        .timeout_connect(std::time::Duration::from_secs(5))
        .timeout(std::time::Duration::from_secs(10))
        .build()
    {
        a => a,
    };
    let _ = agent.get(&cfg.login_page_url()).call();

    let url = build_login_url(cfg, ip, "dr1005", ac);
    log_info!("Auth URL: {url}");

    match agent.get(&url).call() {
        Ok(resp) => {
            let status = resp.status();
            log_info!("HTTP status: {status}");
            if status != 200 {
                return AuthOutcome::NetworkError { msg: format!("HTTP {status}") };
            }
            match resp.into_string() {
                Ok(body) => {
                    log_info!("HTTP Response: {}", truncate(&body, 300));
                    interpret(&body)
                }
                Err(e) => AuthOutcome::NetworkError { msg: format!("读取响应失败: {e}") },
            }
        }
        Err(ureq::Error::Status(code, resp)) => {
            let body = resp.into_string().unwrap_or_default();
            log_warn!("HTTP {code} body: {}", truncate(&body, 300));
            interpret(&body)
        }
        Err(e) => AuthOutcome::NetworkError { msg: e.to_string() },
    }
}

/// 根据 JSONP 响应判定认证结果。
pub fn interpret(jsonp: &str) -> AuthOutcome {
    match parse_jsonp(jsonp) {
        Some((1, _msg, _)) => AuthOutcome::Success,
        Some((0, Some(2), _msg)) => AuthOutcome::AlreadyOnline,
        Some((0, _, msg)) => AuthOutcome::Failed { msg },
        Some((code, _, _)) => AuthOutcome::Failed { msg: format!("未知 result={code}") },
        None => AuthOutcome::Failed { msg: "响应非 JSONP".into() },
    }
}

fn truncate(s: &str, n: usize) -> &str {
    match s.char_indices().nth(n) {
        Some((i, _)) => &s[..i],
        None => s,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg() -> Config {
        let mut c = Config::default();
        c.auth_url = "http://10.10.102.50:801/eportal/portal/login".into();
        c.student_id = "24028116".into();
        c.operator = crate::config::Operator::Unicom;
        c.password = "Tust0910".into();
        c
    }

    #[test]
    fn url_encode_account() {
        assert_eq!(urlencode(",0,24028116@unicom"), "%2C0%2C24028116%40unicom");
        assert_eq!(urlencode("pw/1+2"), "pw%2F1%2B2");
    }

    #[test]
    fn login_url_shape() {
        let url = build_login_url(&cfg(), "10.59.29.29", "dr1005", None);
        assert!(url.starts_with("http://10.10.102.50:801/eportal/portal/login?callback=dr1005&"));
        assert!(url.contains("user_account=%2C0%2C24028116%40unicom"));
        assert!(url.contains("user_password=Tust0910"));
        assert!(url.contains("wlan_user_ip=10.59.29.29"));
        assert!(url.contains("jsVersion=4.1.3"));
    }

    #[test]
    fn interpret_success() {
        let o = interpret(r#"dr1005({"result":1,"msg":"Portal协议认证成功！"});"#);
        assert_eq!(o, AuthOutcome::Success);
    }

    #[test]
    fn interpret_already_online() {
        let o = interpret(r#"dr1005({"result":0,"msg":"IP: 10.48.0.224 已经在线！","ret_code":2});"#);
        assert_eq!(o, AuthOutcome::AlreadyOnline);
    }

    #[test]
    fn interpret_ac_fail() {
        let o = interpret(r#"dr1005({"result":0,"msg":"AC认证失败","ret_code":1});"#);
        assert_eq!(o, AuthOutcome::Failed { msg: "AC认证失败".into() });
    }

    #[test]
    fn interpret_garbage() {
        assert!(matches!(interpret("not jsonp"), AuthOutcome::Failed { .. }));
    }

    #[test]
    fn interpret_success_has_no_ret_code() {
        let o = interpret(r#"dr1005({"result":"1","msg":"ok"});"#);
        // result 为字符串时按失败处理（服务端不会这样发；防御性）
        assert!(matches!(o, AuthOutcome::Failed { .. }));
    }
}
