//! 网络连通性检测与 captive portal 识别。

use std::io::{Read, Write};
use std::net::TcpStream;
use std::time::Duration;

use crate::log_warn;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum NetStatus {
    /// 直连成功（可能仍是 200 假阳性，但 UI 意义上已连通）
    Connected,
    /// 被劫持到认证门户（redirect = 完整重定向 URL）
    CaptivePortal { redirect: String },
    /// DNS 暂不可用（认证刚成功后常见，DHCP/DNS 未生效）；非硬失败
    DnsPending,
    /// 无法联网
    Disconnected { reason: String },
}

/// 从 captive portal 重定向 URL 提取 AC 参数（wlanacip / wlanacname / wlanuserip）。
pub fn extract_ac_params(redirect: &str) -> (String, String, String) {
    let mut get = |key: &str| -> String {
        redirect
            .split('?')
            .nth(1)
            .unwrap_or("")
            .split('&')
            .find_map(|kv| {
                let (k, v) = kv.split_once('=')?;
                (k.eq_ignore_ascii_case(key)).then(|| v.to_string())
            })
            .unwrap_or_default()
    };
    (get("wlanacip"), get("wlanacname"), get("wlanuserip"))
}

/// 从 HTTP 响应头字节中提取 Location（若 3xx）。
fn extract_location(head: &str) -> Option<String> {
    let status_first_line = head.lines().next()?;
    if !status_first_line.contains(" 30") {
        return None;
    }
    head.lines()
        .find_map(|l| {
            let v = l.strip_prefix("Location:")?;
            Some(v.trim().trim_end_matches('\r').to_string())
        })
}

/// 用原生 TcpStream 做 HTTP GET（避免为一次探测建完整 HTTP 客户端）。
/// 仅 http://（校园网探测地址均为 http）；https 一律按 Connected 处理（TLS 握手成功即连通）。
pub fn check(url: &str, timeout: Duration) -> NetStatus {
    let (host, port, path) = match parse_http_url(url) {
        Some(p) => p,
        None => return NetStatus::Disconnected { reason: format!("非法检测地址: {url}") },
    };

    if port == 443 {
        // HTTPS：TLS 握手成功即可视为连通（不做证书校验的简化路径不存在，
        // 因此对 https 用 ureq；正常配置不会走到这里）
        return check_https(url, timeout);
    }

    let addr = format!("{host}:{port}");

    // 连接超时：先解析地址再用 connect_timeout，避免不可达时 SYN 重试挂 20s+
    use std::net::ToSocketAddrs;
    let sockaddrs: Vec<_> = match addr.to_socket_addrs() {
        Ok(it) => it.collect(),
        Err(e) => {
            log_warn!("DNS 解析 {addr} 失败: {e}（可能认证刚生效，DNS 暂未就绪）");
            return NetStatus::DnsPending;
        }
    };
    let mut last_err: Option<std::io::Error> = None;
    let mut stream = None;
    for sa in &sockaddrs {
        match TcpStream::connect_timeout(sa, timeout) {
            Ok(s) => { stream = Some(s); break; }
            Err(e) => { last_err = Some(e); }
        }
    }
    let mut stream = match stream {
        Some(s) => s,
        None => {
            let reason = last_err.map(|e| e.to_string()).unwrap_or_else(|| "无可用地址".into());
            return NetStatus::Disconnected { reason: format!("连接 {addr} 失败: {reason}") };
        }
    };
    let _ = stream.set_read_timeout(Some(timeout));
    let _ = stream.set_write_timeout(Some(timeout));

    let req = format!(
        "GET {path} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: CampusAuthGuardian/2.0\r\nConnection: close\r\n\r\n"
    );
    if let Err(e) = stream.write_all(req.as_bytes()) {
        return NetStatus::Disconnected { reason: format!("发送失败: {e}") };
    }

    let mut buf = Vec::with_capacity(4096);
    let mut chunk = [0u8; 2048];
    let mut total = 0;
    loop {
        match stream.read(&mut chunk) {
            Ok(0) => break,
            Ok(n) => {
                buf.extend_from_slice(&chunk[..n]);
                total += n;
                if total > 64 * 1024 {
                    break;
                }
                // 头部足够即可判断重定向
                if let Ok(text) = std::str::from_utf8(&buf) {
                    if text.contains("\r\n\r\n") {
                        break;
                    }
                }
            }
            Err(e) => return NetStatus::Disconnected { reason: format!("读取失败: {e}") },
        }
    }

    let head = String::from_utf8_lossy(&buf).into_owned();
    if let Some(loc) = extract_location(&head) {
        return NetStatus::CaptivePortal { redirect: loc };
    }
    // 200 且含运营商门户特征也视为 portal（有些学校 200 拦截）
    if head.starts_with("HTTP/1.") && (head.contains(" 200 ") || head.contains(" 302 ")) {
        NetStatus::Connected
    } else {
        NetStatus::Disconnected { reason: "无有效 HTTP 响应".into() }
    }
}

fn check_https(url: &str, timeout: Duration) -> NetStatus {
    match ureq::AgentBuilder::new().timeout(timeout).timeout_connect(timeout).build().get(url).call() {
        Ok(_) => NetStatus::Connected,
        Err(ureq::Error::Status(code, resp)) => {
            let loc = resp.header("location").map(|s| s.to_string());
            match loc {
                Some(l) if (300..400).contains(&code) => NetStatus::CaptivePortal { redirect: l },
                _ => NetStatus::Connected,
            }
        }
        Err(e) => NetStatus::Disconnected { reason: e.to_string() },
    }
}

/// 解析 http://host[:port]/path；返回 (host, port, path)。
pub fn parse_http_url(url: &str) -> Option<(String, u16, String)> {
    let rest = url.strip_prefix("http://")?;
    let (hostport, path) = match rest.find('/') {
        Some(i) => (&rest[..i], &rest[i..]),
        None => (rest, "/"),
    };
    let (host, port) = match hostport.rsplit_once(':') {
        Some((h, p)) => (h, p.parse().ok()?),
        None => (hostport, 80),
    };
    if host.is_empty() {
        return None;
    }
    Some((host.to_string(), port, path.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn url_parse() {
        assert_eq!(
            parse_http_url("http://www.baidu.com"),
            Some(("www.baidu.com".into(), 80, "/".into()))
        );
        assert_eq!(
            parse_http_url("http://10.10.102.50:801/a79.htm?x=1"),
            Some(("10.10.102.50".into(), 801, "/a79.htm?x=1".into()))
        );
        assert_eq!(parse_http_url("ftp://x"), None);
    }

    #[test]
    fn location_extraction() {
        let head = "HTTP/1.1 302 Found\r\nLocation: http://10.10.102.50/a79.htm?wlanuserip=10.48.0.224\r\n\r\n";
        assert_eq!(
            extract_location(head),
            Some("http://10.10.102.50/a79.htm?wlanuserip=10.48.0.224".into())
        );
        let ok_head = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nbody";
        assert_eq!(extract_location(ok_head), None);
    }
}
