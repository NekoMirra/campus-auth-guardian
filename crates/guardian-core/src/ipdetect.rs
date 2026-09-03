//! 本机 IPv4 地址枚举（Windows `GetAdaptersAddresses`，via windows-sys 最小 FFI）。

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AdapterIp {
    pub name: String,
    pub ip: String,
    pub mac: Option<String>,
}

#[cfg(windows)]
mod imp {
    use super::AdapterIp;

    use windows_sys::Win32::Foundation::ERROR_BUFFER_OVERFLOW;
    use windows_sys::Win32::NetworkManagement::IpHelper::{
        GetAdaptersAddresses, GAA_FLAG_SKIP_ANYCAST, GAA_FLAG_SKIP_MULTICAST,
        GAA_FLAG_SKIP_DNS_SERVER, IP_ADAPTER_ADDRESSES_LH,
    };
    use windows_sys::Win32::Networking::WinSock::AF_INET;

    const IF_TYPE_ETHERNET: u32 = 6;
    const IF_TYPE_PPP: u32 = 23;
    const IF_TYPE_IEEE80211: u32 = 71;
    const IF_TYPE_TUNNEL: u32 = 131;

    pub fn list_adapters() -> Vec<AdapterIp> {
        const FLAGS: u32 = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

        let mut size: u32 = 16 * 1024;
        loop {
            let mut buf = vec![0u8; size as usize];
            let rc = unsafe {
                GetAdaptersAddresses(
                    AF_INET as u32,
                    FLAGS,
                    std::ptr::null(),
                    buf.as_mut_ptr() as *mut IP_ADAPTER_ADDRESSES_LH,
                    &mut size,
                )
            };
            match rc {
                0 => return extract(&buf),
                ERROR_BUFFER_OVERFLOW => {
                    if size > 64 * 1024 * 1024 {
                        return Vec::new();
                    }
                    continue;
                }
                _ => return Vec::new(),
            }
        }
    }

    fn extract(buf: &[u8]) -> Vec<AdapterIp> {
        let mut out = Vec::new();
        let mut node = buf.as_ptr() as *const IP_ADAPTER_ADDRESSES_LH;
        while !node.is_null() {
            let a = unsafe { &*node };
            if matches!(a.IfType, IF_TYPE_ETHERNET | IF_TYPE_IEEE80211 | IF_TYPE_PPP | IF_TYPE_TUNNEL) {
                // FriendlyName: PWSTR（NUL 结尾宽字符串）；256 字符上限防畸形数据 UB
                let name = unsafe {
                    let mut l = 0usize;
                    while l < 256 && *a.FriendlyName.add(l) != 0 {
                        l += 1;
                    }
                    String::from_utf16_lossy(std::slice::from_raw_parts(a.FriendlyName, l))
                };
                let mac_len = a.PhysicalAddressLength as usize;
                let mac = if mac_len > 0 && mac_len <= 8 {
                    Some(
                        a.PhysicalAddress[..mac_len]
                            .iter()
                            .map(|b| format!("{b:02X}"))
                            .collect::<Vec<_>>()
                            .join("-"),
                    )
                } else {
                    None
                };
                let mut ua = a.FirstUnicastAddress;
                let mut guard = 0;
                while !ua.is_null() && guard < 64 {
                    guard += 1;
                    let u = unsafe { &*ua };
                    let sa = u.Address.lpSockaddr;
                    if !sa.is_null() {
                        let family = unsafe { (*sa).sa_family };
                        if family as u32 == AF_INET as u32 {
                            // SOCKADDR_IN: family(2) + port(2) + addr(4)
                            let octets = unsafe { *(sa as *const [u8; 8]) };
                            let ip = format!(
                                "{}.{}.{}.{}",
                                octets[4], octets[5], octets[6], octets[7]
                            );
                            out.push(AdapterIp { name: name.clone(), ip, mac: mac.clone() });
                        }
                    }
                    ua = u.Next;
                }
            }
            node = a.Next;
        }
        out
    }
}

#[cfg(not(windows))]
mod imp {
    use super::AdapterIp;
    pub fn list_adapters() -> Vec<AdapterIp> {
        Vec::new()
    }
}

pub use imp::list_adapters;

/// 选择用于认证的本机 IP。
/// 优先级：校园网 10.x > 宿主 192.168.x（非 .1 网关）> 172.16-31.x > 其他非回环。
/// 排除：回环、链路本地 169.254、Tailscale 100.x、APIPA。
pub fn detect_local_ip() -> Option<String> {
    let ips: Vec<String> = list_adapters()
        .into_iter()
        .map(|a| a.ip)
        .filter(|ip| {
            !ip.starts_with("169.254.")
                && ip != "127.0.0.1"
                && !ip.starts_with("100.")
                && !ip.starts_with("100.64.")
        })
        .collect();

    let score = |ip: &str| -> u8 {
        let octets: Vec<u32> = ip.split('.').filter_map(|o| o.parse().ok()).collect();
        if octets.len() != 4 {
            return 0;
        }
        let (a, b, c) = (octets[0], octets[1], octets[2]);
        if a == 10 {
            5 // 校园网/企业内网（ePortal 场景的目标网段）
        } else if a == 192 && b == 168 && c != 1 {
            4 // 宿主网段（非 .1 网关自身）
        } else if a == 192 && b == 168 {
            3 // 192.168.x.1 类（可能是网关/虚拟网卡）
        } else if a == 172 && (16..=31).contains(&b) {
            2 // Docker/Hyper-V 常见段，低优先
        } else {
            1
        }
    };

    ips.into_iter().max_by_key(|ip| score(ip))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_does_not_panic() {
        let _ = detect_local_ip();
    }

    #[test]
    fn prefers_campus_10x() {
        // 无法注入 mock adapter；直接测评分逻辑的等价行为
        let ips = vec![
            "172.29.144.1".to_string(),   // Hyper-V
            "192.168.1.155".to_string(),  // 物理网卡
            "10.48.0.224".to_string(),    // 校园网
            "100.123.202.1".to_string(),  // Tailscale
        ];
        let best = ips
            .into_iter()
            .filter(|ip| !ip.starts_with("169.254.") && ip != "127.0.0.1" && !ip.starts_with("100."))
            .max_by_key(|ip| {
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
            });
        assert_eq!(best.as_deref(), Some("10.48.0.224"));
    }
}
