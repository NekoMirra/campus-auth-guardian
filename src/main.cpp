#pragma execution_character_set("utf-8")

#include <windows.h>
#include <wininet.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <ShlObj.h>
#include <shellapi.h>
#include <vector>

// ============================================================================
// Unicode Support
// ============================================================================
#define MAX_PATH_LEN 512

// ============================================================================
// Configuration
// ============================================================================
struct Config {
    std::wstring auth_url;
    std::wstring check_url;
    int check_interval;
    std::wstring user_account;
    std::wstring user_password;
    std::wstring user_ip;
    std::wstring fixed_ip;  // 固定IP，如果设置则优先使用
    bool guardian_enabled;
    int retry_interval;
    int max_retries;

    bool load(const std::string& path) {
        char buf[1024];
        auto get_val = [&](const char* section, const char* key, std::wstring& dest) {
            GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
            dest = std::wstring(buf, buf + strlen(buf));
            return !dest.empty();
        };
        auto get_int = [&](const char* section, const char* key, int def) {
            return GetPrivateProfileIntA(section, key, def, path.c_str());
        };

        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        std::string dir = exe_path;
        size_t pos = dir.rfind('\\');
        if (pos != std::string::npos) dir = dir.substr(0, pos);
        std::string cfg = dir + "\\config.ini";

        get_val("network", "auth_url", auth_url);
        get_val("network", "check_url", check_url);
        check_interval = get_int("network", "check_interval", 30);
        get_val("account", "user_account", user_account);
        get_val("account", "user_password", user_password);
        get_val("account", "fixed_ip", fixed_ip);  // 读取固定IP配置
        guardian_enabled = get_int("guardian", "enabled", 0) == 1;
        retry_interval = get_int("guardian", "retry_interval", 10);
        max_retries = get_int("guardian", "max_retries", 3);
        return !auth_url.empty() && !user_account.empty() && !user_password.empty();
    }
};

bool ensure_config_exists() {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string dir = exe_path;
    size_t pos = dir.rfind('\\');
    if (pos != std::string::npos) dir = dir.substr(0, pos);

    std::string config_path = dir + "\\config.ini";
    std::string template_path = dir + "\\config.ini.template";

    // If config.ini already exists, no need to create
    {
        std::ifstream test(config_path);
        if (test.good()) return true;
    }

    // Check if template exists
    {
        std::ifstream src(template_path, std::ios::binary);
        std::ofstream dst(config_path, std::ios::binary);
        if (!dst.good()) {
            return false;
        }

        if (src.good()) {
            dst << src.rdbuf();
        } else {
            // Fallback: generate a usable config when template is missing.
            dst << "[network]\n"
                   "auth_url = http://10.10.102.50:801/eportal/portal/login\n"
                   "check_url = http://www.baidu.com\n"
                   "check_interval = 30\n"
                   "\n"
                   "[account]\n"
                   "user_account = ,0,YOUR_STUDENT_ID@unicom\n"
                   "user_password = YOUR_PASSWORD\n"
                   "# fixed_ip = 10.59.29.29\n"
                   "\n"
                   "[guardian]\n"
                   "enabled = 0\n"
                   "retry_interval = 10\n"
                   "max_retries = 3\n";
        }
    }

    return true;
}

Config g_config;

// ============================================================================
// Logging
// ============================================================================
void write_log(const char* format, ...) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string dir = exe_path;
    size_t pos = dir.rfind('\\');
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    std::string log_path = dir + "\\campus_auth.log";

    FILE* fp = fopen(log_path.c_str(), "a");
    if (!fp) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}

// ============================================================================
// HTTP Client with Cookies
// ============================================================================
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string url_encode(const std::string& input) {
    std::string output;
    for (unsigned char c : input) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            output += c;
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            output += buf;
        }
    }
    return output;
}

HINTERNET g_hInternet = NULL;

bool parse_set_cookie_header(const std::string& header_line, std::string& phpsessid) {
    // Look for Set-Cookie: PHPSESSID=xxx; pattern
    std::string lower_line = header_line;
    for (auto& c : lower_line) c = (char)tolower((unsigned char)c);

    if (lower_line.find("set-cookie:") != std::string::npos ||
        lower_line.find("set-cookie :") != std::string::npos) {
        size_t cookie_start = lower_line.find("phpsessid=");
        if (cookie_start != std::string::npos) {
            cookie_start += 10; // len("phpsessid=")
            size_t cookie_end = header_line.find(";", cookie_start);
            if (cookie_end == std::string::npos) cookie_end = header_line.length();
            // Account for potential spaces
            while (cookie_start < cookie_end && header_line[cookie_start] == ' ') cookie_start++;
            phpsessid = header_line.substr(cookie_start, cookie_end - cookie_start);
            write_log("Found PHPSESSID in Set-Cookie: %s", phpsessid.c_str());
            return true;
        }
    }
    return false;
}

std::string fetch_login_page_and_get_cookie(std::string& phpsessid) {
    std::string response;

    // Create fresh connection for each request
    HINTERNET hInternet = InternetOpenA("CampusAuth/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("InternetOpen failed");
        return response;
    }

    std::string base_url = wstring_to_utf8(g_config.auth_url);
    // Extract base URL without path
    size_t pos = base_url.find("://");
    if (pos != std::string::npos) {
        pos = base_url.find("/", pos + 3);
        if (pos != std::string::npos) {
            base_url = base_url.substr(0, pos);
        }
    }

    write_log("Fetching login page: %s", base_url.c_str());

    HINTERNET hUrl = InternetOpenUrlA(hInternet, base_url.c_str(), NULL, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);

    if (!hUrl) {
        DWORD err = GetLastError();
        write_log("InternetOpenUrl failed: %lu", err);
        InternetCloseHandle(hInternet);
        return response;
    }

    // Read response headers to find Set-Cookie
    char header_buf[8192] = {0};
    DWORD header_len = sizeof(header_buf);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, header_buf, &header_len, NULL)) {
        write_log("Response headers:\n%s", header_buf);

        // Parse Set-Cookie headers line by line
        char* ctx;
        char* line = strtok_s(header_buf, "\r\n", &ctx);
        while (line) {
            if (parse_set_cookie_header(line, phpsessid)) {
                break;
            }
            line = strtok_s(NULL, "\r\n", &ctx);
        }
    }

    // Read response body
    char buf[4096];
    DWORD bytesRead;
    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        response += buf;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (!phpsessid.empty()) {
        write_log("PHPSESSID from headers: %s", phpsessid.c_str());
    } else {
        write_log("PHPSESSID not found in response headers");
    }

    return response;
}

std::string build_auth_url(const std::string& mac, const std::string& phpsessid) {
    std::string account = wstring_to_utf8(g_config.user_account);
    std::string password = wstring_to_utf8(g_config.user_password);
    std::string ip = wstring_to_utf8(g_config.user_ip);

    std::string encoded_account = url_encode(account);
    std::string encoded_password = url_encode(password);

    // Use 000000000000 as seen in successful packet capture
    std::string mac_param = "000000000000";

    // Use dr1005 and v=3015 from successful packet capture
    std::string url = wstring_to_utf8(g_config.auth_url) + "?callback=dr1005"
        "&login_method=1"
        "&user_account=" + encoded_account +
        "&user_password=" + encoded_password +
        "&wlan_user_ip=" + ip +
        "&wlan_user_ipv6="
        "&wlan_user_mac=" + mac_param +
        "&wlan_ac_ip="
        "&wlan_ac_name="
        "&jsVersion=4.1.3"
        "&terminal_type=1"
        "&lang=zh-cn"
        "&v=3015"
        "&lang=zh";

    write_log("Auth URL: %s", url.c_str());
    return url;
}

std::string http_get_with_headers(const std::string& url, const std::string& phpsessid) {
    std::string result;

    // Create fresh connection for each request to avoid error 12018
    HINTERNET hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("InternetOpen failed in http_get_with_headers");
        return result;
    }

    // Build URL with cookie as query parameter if we have one
    std::string full_url = url;
    if (!phpsessid.empty()) {
        // Check if URL already has query string
        if (full_url.find('?') != std::string::npos) {
            full_url += "&PHPSESSID=" + phpsessid;
        } else {
            full_url += "?PHPSESSID=" + phpsessid;
        }
    }

    write_log("Making request to: %s", full_url.c_str());

    HINTERNET hUrl = InternetOpenUrlA(hInternet, full_url.c_str(), NULL, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);

    if (!hUrl) {
        DWORD err = GetLastError();
        write_log("InternetOpenUrl failed: %lu", err);
        InternetCloseHandle(hInternet);
        return result;
    }

    char buf[4096];
    DWORD bytesRead;
    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        result += buf;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    write_log("HTTP Response: %s", result.c_str());
    return result;
}

bool check_internet_access() {
    HINTERNET hInternet = InternetOpenA("Checker/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    std::string check_url = wstring_to_utf8(g_config.check_url);
    HINTERNET hUrl = InternetOpenUrlA(hInternet, check_url.c_str(), NULL, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);

    bool success = (hUrl != NULL);
    if (hUrl) InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    write_log("Internet check: %s", success ? "connected" : "disconnected");
    return success;
}

// ============================================================================
// Authentication
// ============================================================================
std::string get_local_ip() {
    write_log("=== Network Adapters ===");

    PIP_ADAPTER_INFO pAdapterInfo = NULL;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);

    // Track best IP - prefer 10.x.x.x > WiFi 192.168.x.x > Ethernet 192.168.x.x > 172.x.x.x
    // For campus networks: 10.x.x.x is most common, but some use 192.168.x.x for WiFi
    std::string ip_best_10;
    std::string ip_wifi_192;
    std::string ip_eth_192;
    std::string ip_172;
    std::string best_adapter_name;

    // Helper to check if adapter is virtual by MAC prefix or Description
    auto is_virtual_adapter = [](PIP_ADAPTER_INFO pAdapter) -> bool {
        // Check MAC prefix for common hypervisors
        if (pAdapter->AddressLength >= 3) {
            // Hyper-V: 00-15-5D-xx-xx-xx
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x15 &&
                pAdapter->Address[2] == 0x5D) {
                return true;
            }
            // VMware: 00-50-56-xx-xx-xx
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x50 &&
                pAdapter->Address[2] == 0x56) {
                return true;
            }
            // VirtualBox: 08-00-27-xx-xx-xx
            if (pAdapter->Address[0] == 0x08 && pAdapter->Address[1] == 0x00 &&
                pAdapter->Address[2] == 0x27) {
                return true;
            }
        }

        // Check Description for virtual keywords
        const char* d = pAdapter->Description;
        if (d) {
            if (strstr(d, "Hyper-V") != NULL || strstr(d, "hyper-v") != NULL ||
                strstr(d, "vEthernet") != NULL || strstr(d, "Virtual") != NULL ||
                strstr(d, "VMware") != NULL || strstr(d, "VirtualBox") != NULL ||
                strstr(d, "VPN") != NULL || strstr(d, "TAP") != NULL ||
                strstr(d, "TUN") != NULL || strstr(d, "WireGuard") != NULL ||
                strstr(d, "OpenVPN") != NULL || strstr(d, "Docker") != NULL) {
                return true;
            }
        }
        return false;
    };

    // Helper to check if IP is from Hyper-V default switch
    auto is_hyperv_ip = [](const char* ip) -> bool {
        // Hyper-V default switch uses 172.17.x.x - 172.31.x.x range
        // But specifically 172.17.240.0/24 is common
        if (strncmp(ip, "172.", 4) == 0) {
            // Could be Hyper-V, but we can't be 100% sure
            // Only skip if it looks like a virtual network IP
            return true; // Be conservative - skip all 172.x.x.x if not sure
        }
        return false;
    };

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
        if (pAdapterInfo) {
            if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == NO_ERROR) {
                for (PIP_ADAPTER_INFO pAdapter = pAdapterInfo; pAdapter; pAdapter = pAdapter->Next) {
                    const char* type_str = "Unknown";
                    if (pAdapter->Type == MIB_IF_TYPE_ETHERNET) type_str = "Ethernet";
                    else if (pAdapter->Type == IF_TYPE_IEEE80211) type_str = "WiFi";

                    bool is_virtual = is_virtual_adapter(pAdapter);

                    IP_ADDR_STRING* pIpAddr = &pAdapter->IpAddressList;
                    while (pIpAddr) {
                        std::string addr = pIpAddr->IpAddress.String;

                        // Log MAC prefix for debugging
                        if (pAdapter->AddressLength >= 3) {
                            write_log("  [%s] %s: %s (MAC: %02X-%02X-%02X-...)",
                                type_str, pAdapter->Description, addr.c_str(),
                                pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2]);
                        } else {
                            if (is_virtual) {
                                write_log("  [SKIP-VIRTUAL:%s] %s: %s",
                                    type_str, pAdapter->Description, addr.c_str());
                            } else {
                                write_log("  [%s] %s: %s",
                                    type_str, pAdapter->Description, addr.c_str());
                            }
                        }

                        // Only consider non-virtual, non-zero IPs
                        if (!is_virtual && addr != "0.0.0.0") {
                            bool is_wifi = (pAdapter->Type == IF_TYPE_IEEE80211);

                            if (addr.find("10.") == 0) {
                                ip_best_10 = addr;
                                best_adapter_name = pAdapter->Description;
                            } else if (addr.find("192.168.") == 0) {
                                if (is_wifi && ip_wifi_192.empty()) {
                                    ip_wifi_192 = addr;
                                } else if (!is_wifi && ip_eth_192.empty()) {
                                    ip_eth_192 = addr;
                                }
                            } else if (addr.find("172.") == 0 && ip_172.empty()) {
                                // Skip 172.x.x.x - likely virtual network
                                write_log("    [SKIP-172: likely virtual network]");
                            }
                        } else if (is_virtual) {
                            write_log("    [SKIP: virtual adapter]");
                        }
                        pIpAddr = pIpAddr->Next;
                    }
                }
            }
            free(pAdapterInfo);
        }
    }
    write_log("=== IP Selection ===");
    write_log("  10.x.x.x found: %s", ip_best_10.empty() ? "(none)" : ip_best_10.c_str());
    write_log("  WiFi 192.168.x.x: %s", ip_wifi_192.empty() ? "(none)" : ip_wifi_192.c_str());
    write_log("  Eth 192.168.x.x: %s", ip_eth_192.empty() ? "(none)" : ip_eth_192.c_str());
    write_log("  172.x.x.x found: %s (skipped)", ip_172.empty() ? "(none)" : ip_172.c_str());
    write_log("  Best adapter: %s", best_adapter_name.empty() ? "(none)" : best_adapter_name.c_str());

    // Priority: 10.x.x.x > WiFi 192.168.x.x > Ethernet 192.168.x.x > 172.x.x.x
    std::string ip;
    if (!ip_best_10.empty()) {
        ip = ip_best_10;
    } else if (!ip_wifi_192.empty()) {
        ip = ip_wifi_192;
    } else if (!ip_eth_192.empty()) {
        ip = ip_eth_192;
    } else if (!ip_172.empty()) {
        ip = ip_172; // Fallback - should rarely happen
    }
    write_log("  Selected: %s", ip.empty() ? "(none)" : ip.c_str());

    if (ip.empty()) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            struct hostent* he = gethostbyname(hostname);
            if (he) {
                struct in_addr addr;
                for (int i = 0; he->h_addr_list[i] != NULL; i++) {
                    memcpy(&addr, he->h_addr_list[i], sizeof(struct in_addr));
                    ip = inet_ntoa(addr);
                    if (ip.find("10.") == 0) break;
                }
            }
        }
    }

    write_log("Local IP: %s", ip.empty() ? "(none)" : ip.c_str());
    return ip;
}

std::string get_local_mac() {
    std::string mac;

    PIP_ADAPTER_INFO pAdapterInfo = NULL;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);

    // Helper to check if adapter is virtual by MAC prefix
    auto is_virtual_mac = [](PIP_ADAPTER_INFO pAdapter) -> bool {
        if (pAdapter->AddressLength >= 3) {
            // Hyper-V: 00-15-5D-xx-xx-xx
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x15 &&
                pAdapter->Address[2] == 0x5D) {
                return true;
            }
            // VMware: 00-50-56-xx-xx-xx
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x50 &&
                pAdapter->Address[2] == 0x56) {
                return true;
            }
            // VirtualBox: 08-00-27-xx-xx-xx
            if (pAdapter->Address[0] == 0x08 && pAdapter->Address[1] == 0x00 &&
                pAdapter->Address[2] == 0x27) {
                return true;
            }
        }
        return false;
    };

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
        if (pAdapterInfo) {
            if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == NO_ERROR) {
                for (PIP_ADAPTER_INFO pAdapter = pAdapterInfo; pAdapter; pAdapter = pAdapter->Next) {
                    // Skip virtual adapters by MAC prefix
                    if (is_virtual_mac(pAdapter)) continue;

                    if (pAdapter->Type == MIB_IF_TYPE_ETHERNET || pAdapter->Type == IF_TYPE_IEEE80211) {
                        IP_ADDR_STRING* pIpAddr = &pAdapter->IpAddressList;
                        while (pIpAddr) {
                            std::string addr = pIpAddr->IpAddress.String;
                            // Only get MAC for non-virtual, non-zero IPs
                            if (addr != "0.0.0.0") {
                                char mac_str[32];
                                snprintf(mac_str, sizeof(mac_str),
                                    "%02X-%02X-%02X-%02X-%02X-%02X",
                                    pAdapter->Address[0], pAdapter->Address[1],
                                    pAdapter->Address[2], pAdapter->Address[3],
                                    pAdapter->Address[4], pAdapter->Address[5]);
                                mac = mac_str;
                                break;
                            }
                            pIpAddr = pIpAddr->Next;
                        }
                    }
                    if (!mac.empty()) break;
                }
            }
            free(pAdapterInfo);
        }
    }

    write_log("Local MAC: %s", mac.empty() ? "(none)" : mac.c_str());
    return mac;
}

bool authenticate(std::string* msg = nullptr) {
    std::string ip;

    // 如果配置了固定IP，优先使用
    if (!g_config.fixed_ip.empty()) {
        ip = wstring_to_utf8(g_config.fixed_ip);
        write_log("Using fixed IP from config: %s", ip.c_str());
    } else {
        ip = get_local_ip();
    }

    std::string mac = get_local_mac();

    if (ip.empty()) {
        if (msg) *msg = "Cannot get local IP";
        write_log("Auth failed: cannot get local IP");
        return false;
    }

    g_config.user_ip = utf8_to_wstring(ip);

    // Step 1: Fetch login page to get PHPSESSID cookie
    std::string phpsessid;
    std::string login_page = fetch_login_page_and_get_cookie(phpsessid);
    if (phpsessid.empty()) {
        write_log("Warning: Could not get PHPSESSID, trying without it");
    }

    // Step 2: Build auth URL and send request
    std::string url = build_auth_url(mac, phpsessid);
    std::string response = http_get_with_headers(url, phpsessid);

    if (response.empty()) {
        if (msg) *msg = "No response from server";
        write_log("Auth failed: empty response");
        return false;
    }

    // Parse JSONP response: dr1010({"result":1,"msg":"xxx"})
    if (response.find("\"result\":1") != std::string::npos ||
        response.find("\"result\": 1") != std::string::npos ||
        response.find("\"result\":1,") != std::string::npos) {
        if (msg) *msg = "Login successful";
        write_log("Auth success");
        return true;
    }

    // Extract error message
    if (msg) {
        size_t start = response.find("\"msg\":\"");
        if (start != std::string::npos) {
            start += 7;
            size_t end = response.find("\"", start);
            if (end != std::string::npos) {
                *msg = response.substr(start, end - start);
            } else {
                *msg = "Login failed";
            }
        } else {
            *msg = "Login failed";
        }
    }

    write_log("Auth failed: %s", msg ? msg->c_str() : "unknown");
    return false;
}

// ============================================================================
// System Tray (Unicode)
// ============================================================================
#define WM_TRAY_ICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_AUTH 1002
#define ID_TRAY_GUARDIAN 1003
#define ID_TRAY_OPEN_WEB 1004
#define ID_TRAY_AUTOSTART 1005
#define ID_TRAY_LOGOUT 1006
#define ID_TRAY_OPEN_CONFIG 1007

std::atomic<bool> g_running{true};
std::atomic<bool> g_guardian_active{false};
std::atomic<bool> g_guardian_enabled{false};
std::atomic<bool> g_auth_in_progress{false};
std::atomic<bool> g_autostart_enabled{false};
HANDLE g_guardian_thread = NULL;

enum class TrayIconState {
    Connected,    // Checkmark icon (green)
    Guardian,     // Shield icon (blue)
    Disconnected, // X icon (red)
    Reconnecting // Animated or different state
};

std::atomic<TrayIconState> g_icon_state{TrayIconState::Connected};

NOTIFYICONDATAW g_nid = {};

// Create a simple colored icon programmatically
HICON create_colored_icon(COLORREF bg_color) {
    int size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    // Fill background with color
    HBRUSH hBrush = CreateSolidBrush(bg_color);
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw simple shape (circle) for cleaner look
    HBRUSH hFillBrush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, hFillBrush);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
    Ellipse(hdcMem, 2, 2, size - 2, size - 2);
    SelectObject(hdcMem, hOldBrush);
    DeleteObject(hFillBrush);
    DeleteObject(hPen);

    SelectObject(hdcMem, hBmpOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask = hBmp;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hBmp);

    return hIcon;
}

// Create checkmark icon (green)
HICON create_checkmark_icon() {
    int size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    // Green background
    HBRUSH hBrush = CreateSolidBrush(RGB(34, 197, 94)); // Green
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw white checkmark
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
    MoveToEx(hdcMem, 4, 8, NULL);
    LineTo(hdcMem, 7, 11);
    LineTo(hdcMem, 12, 5);

    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPen);

    SelectObject(hdcMem, hBmpOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask = hBmp;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hBmp);

    return hIcon;
}

// Create shield icon (blue)
HICON create_shield_icon() {
    int size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    // Blue background
    HBRUSH hBrush = CreateSolidBrush(RGB(59, 130, 246)); // Blue
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw white shield shape
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hFillBrush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, hFillBrush);

    POINT shield[] = {
        {8, 2},   // top
        {13, 4},  // top right
        {13, 8},  // mid right
        {8, 14},  // bottom
        {3, 8},   // mid left
        {3, 4}    // top left
    };
    Polygon(hdcMem, shield, 6);

    // Draw inner shield border
    POINT innerShield[] = {
        {8, 4},   // top
        {11, 5},  // top right
        {11, 8},  // mid right
        {8, 12},  // bottom
        {5, 8},   // mid left
        {5, 5}    // top left
    };
    HBRUSH hInnerBrush = CreateSolidBrush(RGB(59, 130, 246));
    SelectObject(hdcMem, hInnerBrush);
    Polygon(hdcMem, innerShield, 6);
    DeleteObject(hInnerBrush);

    SelectObject(hdcMem, hOldBrush);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hFillBrush);
    DeleteObject(hPen);

    SelectObject(hdcMem, hBmpOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask = hBmp;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hBmp);

    return hIcon;
}

// Create X icon (red)
HICON create_x_icon() {
    int size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    // Red background
    HBRUSH hBrush = CreateSolidBrush(RGB(239, 68, 68)); // Red
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw white X
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);

    MoveToEx(hdcMem, 4, 4, NULL);
    LineTo(hdcMem, 12, 12);
    MoveToEx(hdcMem, 12, 4, NULL);
    LineTo(hdcMem, 4, 12);

    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPen);

    SelectObject(hdcMem, hBmpOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask = hBmp;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hBmp);

    return hIcon;
}

void update_tray_tooltip(const wchar_t* status) {
    std::wstring tooltip = L"[Campus Guardian] ";
    tooltip += status;
    wcsncpy(g_nid.szTip, tooltip.c_str(), sizeof(g_nid.szTip) / sizeof(wchar_t) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void show_notification(const wchar_t* title, const wchar_t* message) {
    g_nid.uFlags = NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, title, sizeof(g_nid.szInfoTitle) / sizeof(wchar_t) - 1);
    wcsncpy(g_nid.szInfo, message, sizeof(g_nid.szInfo) / sizeof(wchar_t) - 1);
    g_nid.uTimeout = 10000;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void update_tray_icon() {
    g_nid.uFlags = NIF_ICON | NIF_TIP;

    HICON icon = NULL;
    switch (g_icon_state) {
        case TrayIconState::Connected:
            icon = create_checkmark_icon();
            break;
        case TrayIconState::Guardian:
            icon = create_shield_icon();
            break;
        case TrayIconState::Disconnected:
        case TrayIconState::Reconnecting:
        default:
            icon = create_x_icon();
            break;
    }

    if (icon) {
        g_nid.hIcon = icon;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        DestroyIcon(icon);
    }
}

void guardian_loop() {
    g_guardian_active = true;
    g_icon_state = TrayIconState::Guardian;
    update_tray_icon();
    write_log("Guardian started");

    while (g_running && g_guardian_enabled) {
        bool connected = check_internet_access();

        if (!connected) {
            g_icon_state = TrayIconState::Disconnected;
            update_tray_icon();
            write_log("Network disconnected, starting auth...");
            update_tray_tooltip(L"Disconnected, reconnecting...");

            std::string msg;
            int retry_count = 0;
            bool success = false;

            while (retry_count < g_config.max_retries && !success && g_running) {
                if (authenticate(&msg)) {
                    success = true;
                    write_log("Auth success!");
                    g_icon_state = TrayIconState::Guardian;
                    update_tray_icon();
                    update_tray_tooltip(L"Connected");
                    show_notification(L"Campus Guardian", L"Network reconnected!");
                } else {
                    retry_count++;
                    write_log("Auth failed (%d/%d): %s", retry_count, g_config.max_retries, msg.c_str());
                    if (retry_count < g_config.max_retries) {
                        std::this_thread::sleep_for(std::chrono::seconds(g_config.retry_interval));
                    }
                }
            }

            if (!success && g_running) {
                write_log("All auth attempts failed!");
                g_icon_state = TrayIconState::Disconnected;
                update_tray_icon();
                update_tray_tooltip(L"Auth failed!");
                show_notification(L"Campus Guardian - Auth Failed",
                    L"Network auth failed after multiple retries. Please check your credentials.");
            }
        }

        if (g_running && g_guardian_enabled) {
            for (int i = 0; i < g_config.check_interval && g_running && g_guardian_enabled; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    g_guardian_active = false;
    g_icon_state = TrayIconState::Connected;
    update_tray_icon();
    write_log("Guardian stopped");
}

void toggle_guardian() {
    g_guardian_enabled = !g_guardian_enabled;

    if (g_guardian_enabled) {
        g_guardian_thread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            guardian_loop();
            return 0;
        }, NULL, 0, NULL);
        update_tray_tooltip(L"Guardian ON");
    } else {
        update_tray_tooltip(L"Guardian OFF");
    }
}

bool is_autostart_enabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[512] = {0};
        DWORD len = sizeof(buf);
        bool exists = RegQueryValueExA(hKey, "CampusAuthGuardian", NULL, NULL,
            (LPBYTE)buf, &len) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        return exists;
    }
    return false;
}

void set_autostart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            char exe_path[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path, MAX_PATH);
            RegSetValueExA(hKey, "CampusAuthGuardian", 0, REG_SZ,
                (const BYTE*)exe_path, (DWORD)strlen(exe_path) + 1);
            write_log("Autostart enabled");
        } else {
            RegDeleteValueA(hKey, "CampusAuthGuardian");
            write_log("Autostart disabled");
        }
        RegCloseKey(hKey);
        g_autostart_enabled = enable;
    }
}

void toggle_autostart() {
    set_autostart(!g_autostart_enabled);
}

void open_login_web() {
    ShellExecuteW(NULL, L"open",
        L"http://10.10.102.50/",
        NULL, NULL, SW_SHOWNORMAL);
}

void open_config_file() {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string dir = exe_path;
    size_t pos = dir.rfind('\\');
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    std::string config_path = dir + "\\config.ini";
    ShellExecuteA(NULL, "open", config_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

bool logout() {
    write_log("Logout started");

    std::string ip;
    if (!g_config.fixed_ip.empty()) {
        ip = wstring_to_utf8(g_config.fixed_ip);
    } else {
        ip = get_local_ip();
    }

    if (ip.empty()) {
        write_log("Logout failed: cannot get local IP");
        return false;
    }

    write_log("Logout IP: %s", ip.c_str());

    // Build logout URL
    std::string url = wstring_to_utf8(g_config.auth_url);
    size_t login_pos = url.find("/login");
    if (login_pos != std::string::npos) {
        url.replace(login_pos, 6, "/logout");
    }

    url += "?callback=dr1003&login_method=1&user_account=drcom&user_password=123"
           "&ac_logout=1&register_mode=1"
           "&wlan_user_ip=" + ip +
           "&wlan_user_ipv6=&wlan_vlan_id=1"
           "&wlan_user_mac=000000000000"
           "&wlan_ac_ip=&wlan_ac_name="
           "&jsVersion=4.1.3&v=10093&lang=zh";

    write_log("Logout URL: %s", url.c_str());

    HINTERNET hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("InternetOpen failed in logout");
        return false;
    }

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);

    std::string response;
    if (hUrl) {
        char buf[4096];
        DWORD bytesRead;
        while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0';
            response += buf;
        }
        InternetCloseHandle(hUrl);
    }
    InternetCloseHandle(hInternet);

    write_log("Logout response: %s", response.c_str());

    // Check for success
    if (response.find("\"result\":1") != std::string::npos ||
        response.find("\"result\": 1") != std::string::npos) {
        write_log("Logout success");
        return true;
    }

    write_log("Logout may have failed");
    return false;
}

void manual_auth() {
    if (g_auth_in_progress) return;
    g_auth_in_progress = true;

    write_log("Manual auth started");
    std::string msg;
    bool success = authenticate(&msg);

    if (success) {
        update_tray_tooltip(L"Auth success");
        show_notification(L"Campus Auth", L"Login successful!");
    } else {
        std::wstring wmsg = utf8_to_wstring(msg);
        update_tray_tooltip(L"Auth failed");
        show_notification(L"Campus Auth", (L"Login failed: " + wmsg).c_str());
    }

    g_auth_in_progress = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();

            wchar_t auth_text[64] = L"Manual Auth";
            if (g_auth_in_progress) {
                wcscpy(auth_text, L"Authenticating...");
            }

            AppendMenuW(hMenu, MF_STRING, ID_TRAY_AUTH, auth_text);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_WEB, L"Open Login Page");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING | (g_guardian_enabled ? MF_CHECKED : 0), ID_TRAY_GUARDIAN, L"Guardian Mode");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_LOGOUT, L"Logout");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_CONFIG, L"Open Config");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_AUTH:
            if (!g_auth_in_progress) {
                std::thread t(manual_auth);
                t.detach();
            }
            break;
        case ID_TRAY_GUARDIAN:
            toggle_guardian();
            break;
        case ID_TRAY_AUTOSTART:
            toggle_autostart();
            break;
        case ID_TRAY_OPEN_WEB:
            open_login_web();
            break;
        case ID_TRAY_LOGOUT:
            {
                std::thread t([]() {
                    if (logout()) {
                        update_tray_tooltip(L"Logged out");
                        show_notification(L"Campus Guardian", L"Logout successful!");
                    } else {
                        update_tray_tooltip(L"Logout failed");
                        show_notification(L"Campus Guardian", L"Logout may have failed. Check log for details.");
                    }
                });
                t.detach();
            }
            break;
        case ID_TRAY_OPEN_CONFIG:
            open_config_file();
            break;
        case ID_TRAY_EXIT:
            g_running = false;
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        break;

    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND create_window() {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"CampusGuardianClass";
    RegisterClassExW(&wc);

    return CreateWindowW(L"CampusGuardianClass", L"Campus Guardian",
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
}

bool init_tray(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_ASTERISK);
    wcscpy(g_nid.szTip, L"[Campus Guardian] Ready");

    return Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

int main_loop() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    HWND hwnd = create_window();
    if (!hwnd) {
        write_log("Failed to create window");
        return 1;
    }

    if (!init_tray(hwnd)) {
        write_log("Failed to create tray icon");
        MessageBoxW(NULL, L"Failed to create system tray icon", L"Error", MB_ICONERROR);
        return 1;
    }

    // Check if autostart is enabled
    g_autostart_enabled = is_autostart_enabled();

    write_log("Application started");

    if (g_config.guardian_enabled) {
        toggle_guardian();
    }

    MSG msg;
    while (g_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running = false;
    if (g_guardian_thread) CloseHandle(g_guardian_thread);
    if (g_hInternet) InternetCloseHandle(g_hInternet);

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    write_log("Application exit");
    return 0;
}

// ============================================================================
// Application Entry
// ============================================================================
static int app_main(bool console_mode) {

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string dir = exe_path;
    size_t pos = dir.rfind('\\');
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    std::string cfg_path = dir + "\\config.ini";

    // Ensure config.ini exists (create from template if needed)
    if (!ensure_config_exists()) {
        if (console_mode) {
            printf("Error: Failed to create config.ini\n");
        } else {
            MessageBoxW(NULL, L"Failed to create config.ini from template", L"Error", MB_ICONERROR);
        }
        return 1;
    }

    write_log("Loading config from: %s", cfg_path.c_str());

    if (!g_config.load(cfg_path)) {
        write_log("Config load failed");
        if (console_mode) {
            printf("Error: Config file not loaded, please check config.ini\n");
        } else {
            MessageBoxW(NULL, L"Config file not loaded, please check config.ini", L"Error", MB_ICONERROR);
        }
        return 1;
    }

    write_log("Config loaded successfully");

    if (console_mode) {
        FreeConsole();

        std::string msg;
        bool success = authenticate(&msg);
        printf("Auth result: %s\n", success ? "Success" : "Failed");
        if (!msg.empty()) printf("Info: %s\n", msg.c_str());
        printf("Check log file for details: %s\\campus_auth.log\n", dir.c_str());
        return success ? 0 : 1;
    }

    return main_loop();
}

int main(int argc, char* argv[]) {
    bool console_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--console") == 0) {
            console_mode = true;
        }
    }

    return app_main(console_mode);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool console_mode = false;

    if (argv != NULL) {
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--console") == 0) {
                console_mode = true;
                break;
            }
        }
        LocalFree(argv);
    }

    return app_main(console_mode);
}