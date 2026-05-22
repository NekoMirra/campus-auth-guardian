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
#include <Shlwapi.h>
#include <vector>
#include <algorithm>
#include <objbase.h>
#include <ole2.h>
#include <propkey.h>

// ============================================================================
// Unicode Support
// ============================================================================
#define MAX_PATH_LEN 512

// Forward declarations
std::wstring utf8_to_wstring(const std::string& str);
std::string wstring_to_utf8(const std::wstring& wstr);

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
    std::wstring fixed_ip;
    bool guardian_enabled;
    int retry_interval;
    int max_retries;

    std::wstring student_id;
    std::wstring operator_type;  // campus, cmcc, unicom, telecom

    void build_user_account() {
        if (!student_id.empty() && !operator_type.empty()) {
            std::string id = wstring_to_utf8(student_id);
            std::string op = wstring_to_utf8(operator_type);
            std::string built = ",0," + id + "@" + op;
            user_account = utf8_to_wstring(built);
        }
    }

    bool load(const std::string& path) {
        std::wstring wpath(path.begin(), path.end());
        wchar_t buf[1024];
        auto get_val = [&](const wchar_t* section, const wchar_t* key, std::wstring& dest) {
            GetPrivateProfileStringW(section, key, L"", buf, sizeof(buf) / sizeof(wchar_t), wpath.c_str());
            dest = buf;
            return !dest.empty();
        };
        auto get_int = [&](const wchar_t* section, const wchar_t* key, int def) {
            return GetPrivateProfileIntW(section, key, def, wpath.c_str());
        };

        get_val(L"network", L"auth_url", auth_url);
        get_val(L"network", L"check_url", check_url);
        check_interval = get_int(L"network", L"check_interval", 30);
        get_val(L"account", L"user_password", user_password);
        get_val(L"account", L"fixed_ip", fixed_ip);
        guardian_enabled = get_int(L"guardian", L"enabled", 0) == 1;
        retry_interval = get_int(L"guardian", L"retry_interval", 10);
        max_retries = get_int(L"guardian", L"max_retries", 3);

        // Load separate fields
        get_val(L"account", L"student_id", student_id);
        get_val(L"account", L"operator_type", operator_type);

        if (!student_id.empty() && !operator_type.empty()) {
            build_user_account();
        } else {
            // Fallback: parse old user_account format
            get_val(L"account", L"user_account", user_account);
            if (!user_account.empty()) {
                std::string acct = wstring_to_utf8(user_account);
                size_t at_pos = acct.find('@');
                size_t comma_pos = acct.rfind(',', at_pos);
                if (at_pos != std::string::npos && comma_pos != std::string::npos) {
                    student_id = utf8_to_wstring(acct.substr(comma_pos + 1, at_pos - comma_pos - 1));
                    operator_type = utf8_to_wstring(acct.substr(at_pos + 1));
                }
            }
        }

        return !auth_url.empty() && !user_account.empty() && !user_password.empty();
    }

    void reload(const std::string& path) {
        load(path);
    }

    void save_operator(const std::string& config_path) {
        std::wstring wpath(config_path.begin(), config_path.end());
        WritePrivateProfileStringW(L"account", L"operator_type",
            operator_type.c_str(), wpath.c_str());
        WritePrivateProfileStringW(L"account", L"student_id",
            student_id.c_str(), wpath.c_str());
    }
};

// Embedded config template — no need for external template file
static const char* const CONFIG_TEMPLATE =
    "# Campus Auth Guardian \xe9\x85\x8d\xe7\xbd\xae\xe6\x96\x87\xe4\xbb\xb6\n"
    "# \xe6\xb3\xa8\xe6\x84\x8f\xef\xbc\x9a\xe6\xad\xa4\xe6\x96\x87\xe4\xbb\xb6\xe5\xbf\x85\xe9\xa1\xbb\xe4\xbd\xbf\xe7\x94\xa8 UTF-8 \xe7\xbc\x96\xe7\xa0\x81\xe4\xbf\x9d\xe5\xad\x98\xef\xbc\x81\n"
    "\n"
    "[network]\n"
    "# --- \xe7\xbd\x91\xe7\xbb\x9c\xe9\x85\x8d\xe7\xbd\xae ---\n"
    "# \xe6\xa0\xa1\xe5\x9b\xad\xe7\xbd\x91\xe8\xae\xa4\xe8\xaf\x81\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe5\x9c\xb0\xe5\x9d\x80\n"
    "auth_url = http://10.10.102.50:801/eportal/portal/login\n"
    "\n"
    "# \xe7\xbd\x91\xe7\xbb\x9c\xe8\xbf\x9e\xe9\x80\x9a\xe6\x80\xa7\xe6\xa3\x80\xe6\xb5\x8b\xe5\x9c\xb0\xe5\x9d\x80\n"
    "check_url = http://www.baidu.com\n"
    "\n"
    "# \xe7\xbd\x91\xe7\xbb\x9c\xe6\xa3\x80\xe6\xb5\x8b\xe9\x97\xb4\xe9\x9a\x94\xef\xbc\x88\xe7\xa7\x92\xef\xbc\x89\n"
    "check_interval = 30\n"
    "\n"
    "[account]\n"
    "# --- \xe8\xb4\xa6\xe5\x8f\xb7\xe9\x85\x8d\xe7\xbd\xae ---\n"
    "# \xe5\xad\xa6\xe5\x8f\xb7\n"
    "student_id = YOUR_STUDENT_ID\n"
    "\n"
    "# \xe8\xbf\x90\xe8\x90\xa5\xe5\x95\x86\xe7\xb1\xbb\xe5\x9e\x8b\xef\xbc\x9a campus / cmcc / unicom / telecom\n"
    "operator_type = unicom\n"
    "\n"
    "# \xe8\xb4\xa6\xe5\x8f\xb7\xe5\xaf\x86\xe7\xa0\x81\n"
    "user_password = YOUR_PASSWORD\n"
    "\n"
    "# \xe5\x9b\xba\xe5\xae\x9aIP\xe5\x9c\xb0\xe5\x9d\x80\xef\xbc\x88\xe5\x8f\xaf\xe9\x80\x89\xef\xbc\x8c\xe7\x95\x99\xe7\xa9\xba\xe5\x88\x99\xe8\x87\xaa\xe5\x8a\xa8\xe6\xa3\x80\xe6\xb5\x8b\xef\xbc\x89\n"
    "fixed_ip =\n"
    "\n"
    "[guardian]\n"
    "# --- \xe5\xae\x88\xe6\x8a\xa4\xe6\xa8\xa1\xe5\xbc\x8f\xe9\x85\x8d\xe7\xbd\xae ---\n"
    "# \xe6\x98\xaf\xe5\x90\xa6\xe9\xbb\x98\xe8\xae\xa4\xe5\x90\xaf\xe7\x94\xa8\xe5\xae\x88\xe6\x8a\xa4\xe6\xa8\xa1\xe5\xbc\x8f (0/1)\n"
    "enabled = 0\n"
    "\n"
    "# \xe8\xae\xa4\xe8\xaf\x81\xe5\xa4\xb1\xe8\xb4\xa5\xe5\x90\x8e\xe9\x87\x8d\xe8\xaf\x95\xe9\x97\xb4\xe9\x9a\x94\xef\xbc\x88\xe7\xa7\x92\xef\xbc\x89\n"
    "retry_interval = 10\n"
    "\n"
    "# \xe6\x9c\x80\xe5\xa4\xa7\xe9\x87\x8d\xe8\xaf\x95\xe6\xac\xa1\xe6\x95\xb0\n"
    "max_retries = 3\n";

// Get exe directory as UTF-8 string (ARM64-safe)
std::string get_exe_dir() {
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    std::wstring wdir = exe_path;
    size_t pos = wdir.rfind(L'\\');
    if (pos != std::wstring::npos) wdir = wdir.substr(0, pos);
    return wstring_to_utf8(wdir);
}

std::string get_config_path() {
    return get_exe_dir() + "\\config.ini";
}

bool ensure_config_exists() {
    std::string config_path = get_config_path();

    {
        std::ifstream test(config_path);
        if (test.good()) return true;
    }

    {
        std::ofstream dst(config_path, std::ios::binary);
        if (!dst.good()) return false;
        dst << CONFIG_TEMPLATE;
    }

    return true;
}

Config g_config;

// ============================================================================
// Logging with rotation
// ============================================================================
static const size_t MAX_LOG_SIZE = 512 * 1024; // 512KB

void rotate_log(const std::string& log_path) {
    std::ifstream f(log_path, std::ios::binary | std::ios::ate);
    if (!f.good()) return;
    size_t size = (size_t)f.tellg();
    f.close();

    if (size < MAX_LOG_SIZE) return;

    // Keep last 25% of log
    size_t keep = size / 4;
    std::vector<char> buf(keep);
    {
        std::ifstream src(log_path, std::ios::binary);
        src.seekg(size - keep);
        src.read(buf.data(), keep);
    }
    {
        std::ofstream dst(log_path, std::ios::binary | std::ios::trunc);
        dst.write(buf.data(), keep);
    }
}

void write_log(const char* format, ...) {
    static std::string log_dir;
    if (log_dir.empty()) {
        log_dir = get_exe_dir();
    }

    std::string log_path = log_dir + "\\campus_auth.log";
    rotate_log(log_path);

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
// HTTP Client with Cookies + Timeout
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

void set_http_timeout(HINTERNET hHandle, int connect_ms = 5000, int send_ms = 5000, int recv_ms = 10000) {
    InternetSetOptionA(hHandle, INTERNET_OPTION_CONNECT_TIMEOUT, &connect_ms, sizeof(connect_ms));
    InternetSetOptionA(hHandle, INTERNET_OPTION_SEND_TIMEOUT, &send_ms, sizeof(send_ms));
    InternetSetOptionA(hHandle, INTERNET_OPTION_RECEIVE_TIMEOUT, &recv_ms, sizeof(recv_ms));
    InternetSetOptionA(hHandle, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, &recv_ms, sizeof(recv_ms));
}

bool parse_set_cookie_header(const std::string& header_line, std::string& phpsessid) {
    std::string lower_line = header_line;
    for (auto& c : lower_line) c = (char)tolower((unsigned char)c);

    if (lower_line.find("set-cookie:") != std::string::npos ||
        lower_line.find("set-cookie :") != std::string::npos) {
        size_t cookie_start = lower_line.find("phpsessid=");
        if (cookie_start != std::string::npos) {
            cookie_start += 10;
            size_t cookie_end = header_line.find(";", cookie_start);
            if (cookie_end == std::string::npos) cookie_end = header_line.length();
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

    HINTERNET hInternet = InternetOpenA("CampusAuth/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("InternetOpen failed: %lu", GetLastError());
        return response;
    }
    set_http_timeout(hInternet, 5000, 5000, 8000);

    // Extract base URL from auth_url (host:port only)
    std::string base_url = wstring_to_utf8(g_config.auth_url);
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

    char header_buf[8192] = {0};
    DWORD header_len = sizeof(header_buf);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, header_buf, &header_len, NULL)) {
        char* ctx;
        char* line = strtok_s(header_buf, "\r\n", &ctx);
        while (line) {
            if (parse_set_cookie_header(line, phpsessid)) break;
            line = strtok_s(NULL, "\r\n", &ctx);
        }
    }

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
        write_log("PHPSESSID not found in response headers, continuing without it");
    }

    return response;
}

std::string build_auth_url(const std::string& /*mac*/, const std::string& phpsessid) {
    std::string account = wstring_to_utf8(g_config.user_account);
    std::string password = wstring_to_utf8(g_config.user_password);
    std::string ip = wstring_to_utf8(g_config.user_ip);

    std::string encoded_account = url_encode(account);
    std::string encoded_password = url_encode(password);

    std::string mac_param = "000000000000";

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

    if (!phpsessid.empty()) {
        url += "&PHPSESSID=" + phpsessid;
    }

    write_log("Auth URL: %s", url.c_str());
    return url;
}

std::string http_get_with_cookie(const std::string& url, const std::string& phpsessid) {
    std::string result;

    HINTERNET hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("InternetOpen failed in http_get: %lu", GetLastError());
        return result;
    }
    set_http_timeout(hInternet, 5000, 5000, 10000);

    // Build Cookie header if we have PHPSESSID
    std::string headers;
    if (!phpsessid.empty()) {
        headers = "Cookie: PHPSESSID=" + phpsessid + "\r\n";
    }

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(),
        headers.empty() ? NULL : headers.c_str(),
        headers.empty() ? 0 : (DWORD)headers.length(),
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);

    if (!hUrl) {
        DWORD err = GetLastError();
        write_log("InternetOpenUrl failed: %lu", err);
        InternetCloseHandle(hInternet);
        return result;
    }

    // Check HTTP status code
    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_len, NULL)) {
        write_log("HTTP status: %lu", status_code);
        if (status_code >= 400) {
            char buf[4096];
            DWORD bytesRead;
            while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                result += buf;
            }
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            write_log("HTTP error response: %s", result.c_str());
            return result;
        }
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

// ============================================================================
// Network Connectivity Check (Captive Portal Aware)
// ============================================================================
enum class NetStatus {
    Connected,        // Real internet access
    CaptivePortal,    // Redirected to auth portal (need to auth)
    Disconnected,     // No network at all
    ServerError       // Network up but check server failed
};

NetStatus check_internet_access() {
    HINTERNET hInternet = InternetOpenA("Checker/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        write_log("Internet check: InternetOpen failed (%lu)", GetLastError());
        return NetStatus::Disconnected;
    }
    set_http_timeout(hInternet, 3000, 3000, 5000);

    std::string check_url = wstring_to_utf8(g_config.check_url);

    HINTERNET hUrl = InternetOpenUrlA(hInternet, check_url.c_str(), NULL, 0,
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD |
        INTERNET_FLAG_NO_AUTO_REDIRECT, 0);

    if (!hUrl) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        // 12007 = DNS error, 12002 = timeout, 12029 = cannot connect
        if (err == ERROR_INTERNET_NAME_NOT_RESOLVED || err == ERROR_INTERNET_TIMEOUT ||
            err == ERROR_INTERNET_CANNOT_CONNECT) {
            write_log("Internet check: disconnected (err=%lu)", err);
            return NetStatus::Disconnected;
        }
        write_log("Internet check: server error (err=%lu)", err);
        return NetStatus::ServerError;
    }

    // Check HTTP status code
    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_len, NULL);

    // Check for redirect - captive portals typically redirect (302/307)
    if (status_code == 302 || status_code == 307 || status_code == 301) {
        char location[1024] = {0};
        DWORD loc_len = sizeof(location);
        if (HttpQueryInfoA(hUrl, HTTP_QUERY_LOCATION, location, &loc_len, NULL)) {
            write_log("Internet check: redirect to %s (captive portal)", location);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return NetStatus::CaptivePortal;
        }
    }

    // Read partial body to check content
    char buf[2048] = {0};
    DWORD bytesRead = 0;
    InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead);
    buf[bytesRead] = '\0';

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (status_code == 200 && bytesRead > 0) {
        // Check if response looks like a captive portal redirect page
        std::string body(buf, bytesRead);
        // Common captive portal indicators
        if (body.find("10.10.102.50") != std::string::npos ||
            body.find("eportal") != std::string::npos ||
            body.find("Dr.COM") != std::string::npos ||
            body.find("PortalServer") != std::string::npos) {
            write_log("Internet check: captive portal page detected");
            return NetStatus::CaptivePortal;
        }
        write_log("Internet check: connected");
        return NetStatus::Connected;
    }

    write_log("Internet check: unexpected status %lu", status_code);
    return NetStatus::ServerError;
}

// ============================================================================
// Authentication
// ============================================================================
std::string get_local_ip() {
    write_log("=== Network Adapters ===");

    PIP_ADAPTER_INFO pAdapterInfo = NULL;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);

    std::string ip_best_10;
    std::string ip_wifi_192;
    std::string ip_eth_192;
    std::string best_adapter_name;

    auto is_virtual_adapter = [](PIP_ADAPTER_INFO pAdapter) -> bool {
        if (pAdapter->AddressLength >= 3) {
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x15 &&
                pAdapter->Address[2] == 0x5D) return true;
            if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x50 &&
                pAdapter->Address[2] == 0x56) return true;
            if (pAdapter->Address[0] == 0x08 && pAdapter->Address[1] == 0x00 &&
                pAdapter->Address[2] == 0x27) return true;
        }

        const char* d = pAdapter->Description;
        if (d) {
            if (strstr(d, "Hyper-V") || strstr(d, "hyper-v") ||
                strstr(d, "vEthernet") || strstr(d, "Virtual") ||
                strstr(d, "VMware") || strstr(d, "VirtualBox") ||
                strstr(d, "VPN") || strstr(d, "TAP") ||
                strstr(d, "TUN") || strstr(d, "WireGuard") ||
                strstr(d, "OpenVPN") || strstr(d, "Docker")) {
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
                    const char* type_str = "Unknown";
                    if (pAdapter->Type == MIB_IF_TYPE_ETHERNET) type_str = "Ethernet";
                    else if (pAdapter->Type == IF_TYPE_IEEE80211) type_str = "WiFi";

                    bool is_virtual = is_virtual_adapter(pAdapter);

                    IP_ADDR_STRING* pIpAddr = &pAdapter->IpAddressList;
                    while (pIpAddr) {
                        std::string addr = pIpAddr->IpAddress.String;

                        if (pAdapter->AddressLength >= 3) {
                            write_log("  [%s] %s: %s (MAC: %02X-%02X-%02X-...)",
                                type_str, pAdapter->Description, addr.c_str(),
                                pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2]);
                        }

                        if (!is_virtual && addr != "0.0.0.0") {
                            bool is_wifi = (pAdapter->Type == IF_TYPE_IEEE80211);

                            if (addr.find("10.") == 0) {
                                ip_best_10 = addr;
                                best_adapter_name = pAdapter->Description;
                            } else if (addr.find("192.168.") == 0) {
                                if (is_wifi && ip_wifi_192.empty()) ip_wifi_192 = addr;
                                else if (!is_wifi && ip_eth_192.empty()) ip_eth_192 = addr;
                            } else if (addr.find("172.") == 0) {
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
    write_log("  10.x.x.x: %s", ip_best_10.empty() ? "(none)" : ip_best_10.c_str());
    write_log("  WiFi 192.168: %s", ip_wifi_192.empty() ? "(none)" : ip_wifi_192.c_str());
    write_log("  Eth 192.168: %s", ip_eth_192.empty() ? "(none)" : ip_eth_192.c_str());
    write_log("  Best adapter: %s", best_adapter_name.empty() ? "(none)" : best_adapter_name.c_str());

    std::string ip;
    if (!ip_best_10.empty()) ip = ip_best_10;
    else if (!ip_wifi_192.empty()) ip = ip_wifi_192;
    else if (!ip_eth_192.empty()) ip = ip_eth_192;

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

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
        if (pAdapterInfo) {
            if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == NO_ERROR) {
                for (PIP_ADAPTER_INFO pAdapter = pAdapterInfo; pAdapter; pAdapter = pAdapter->Next) {
                    bool is_virtual = false;
                    if (pAdapter->AddressLength >= 3) {
                        if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x15 && pAdapter->Address[2] == 0x5D) is_virtual = true;
                        if (pAdapter->Address[0] == 0x00 && pAdapter->Address[1] == 0x50 && pAdapter->Address[2] == 0x56) is_virtual = true;
                        if (pAdapter->Address[0] == 0x08 && pAdapter->Address[1] == 0x00 && pAdapter->Address[2] == 0x27) is_virtual = true;
                    }
                    if (is_virtual) continue;

                    if (pAdapter->Type == MIB_IF_TYPE_ETHERNET || pAdapter->Type == IF_TYPE_IEEE80211) {
                        IP_ADDR_STRING* pIpAddr = &pAdapter->IpAddressList;
                        while (pIpAddr) {
                            std::string addr = pIpAddr->IpAddress.String;
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

// Parse JSONP response fields
struct AuthResult {
    int result = -1;       // 0=fail, 1=success
    int ret_code = -1;     // eportal specific error code
    std::string msg;
};

AuthResult parse_jsonp_response(const std::string& response) {
    AuthResult ar;

    // Extract content between parentheses: dr1005({...})
    size_t start = response.find("({");
    size_t end = response.rfind("})");
    if (start == std::string::npos || end == std::string::npos) {
        ar.msg = "Invalid response format";
        return ar;
    }

    std::string json = response.substr(start + 2, end - start - 2);

    // Parse "result"
    {
        size_t pos = json.find("\"result\":");
        if (pos != std::string::npos) {
            pos += 9;
            while (pos < json.size() && json[pos] == ' ') pos++;
            ar.result = atoi(json.c_str() + pos);
        }
    }

    // Parse "ret_code"
    {
        size_t pos = json.find("\"ret_code\":");
        if (pos != std::string::npos) {
            pos += 11;
            while (pos < json.size() && json[pos] == ' ') pos++;
            ar.ret_code = atoi(json.c_str() + pos);
        }
    }

    // Parse "msg"
    {
        size_t pos = json.find("\"msg\":\"");
        if (pos != std::string::npos) {
            pos += 7;
            size_t end_pos = json.find("\"", pos);
            if (end_pos != std::string::npos) {
                ar.msg = json.substr(pos, end_pos - pos);
            }
        }
    }

    return ar;
}

// Map ret_code to user-friendly Chinese message (based on portal a40.js portal_login_err)
// ret_code=2 (IP already online) is treated as success by the caller, not here
std::string get_auth_error_message(int result, int ret_code, const std::string& msg) {
    // result=1 is always success regardless of ret_code
    if (result == 1) return "认证成功";

    // EPortal ret_code mapping (from Dr.COM portal JavaScript a40.js)
    switch (ret_code) {
        case 1:  return "AC认证失败 - 可能IP地址不匹配或账号异常";
        case 2:  return "终端IP已经在线";
        case 3:  return "系统繁忙，请稍后重试";
        case 5:  return "REQ_CHALLENGE失败，请检查AC确认";
        case 6:  return "REQ_CHALLENGE超时，请检查AC确认";
        case 7:  return "Radius认证失败";
        case 8:  return "Radius认证超时";
        case 9:  return "Radius计费失败";
        case 10: return "Radius计费超时";
        case 11: return "服务器维护中，请稍后重试";
        case 998: return "Portal协议异常不全，请稍后重试";
        default: break;
    }

    // Fallback to server-provided msg (often contains useful detail like "IP: x.x.x.x 已经在线！")
    if (!msg.empty()) return msg;

    return "AC认证失败(未知错误)";
}

bool do_auth_with_ip(const std::string& ip, const std::string& mac, const std::string& phpsessid, std::string* msg) {
    g_config.user_ip = utf8_to_wstring(ip);
    std::string url = build_auth_url(mac, phpsessid);
    std::string response = http_get_with_cookie(url, phpsessid);

    if (response.empty()) {
        if (msg) *msg = "服务器无响应 - 请检查网络连接";
        write_log("Auth failed: empty response");
        return false;
    }

    // Check for HTTP error page (not JSONP)
    if (response.find("dr1005(") == std::string::npos &&
        response.find("dr1003(") == std::string::npos) {
        if (response.find("404") != std::string::npos ||
            response.find("Error") != std::string::npos ||
            response.find("Not Found") != std::string::npos) {
            if (msg) *msg = "认证服务器地址错误(404) - 请检查config.ini中auth_url是否包含端口号801";
            write_log("Auth failed: server returned error page, auth_url may be wrong (missing :801 port?)");
            return false;
        }
        if (msg) *msg = "服务器返回非预期响应";
        write_log("Auth failed: non-JSONP response: %s", response.substr(0, 200).c_str());
        return false;
    }

    // Parse JSONP response
    AuthResult ar = parse_jsonp_response(response);
    std::string error_msg = get_auth_error_message(ar.result, ar.ret_code, ar.msg);

    // result=1: login success
    // result=0, ret_code=2: IP already online — treat as success
    if (ar.result == 1) {
        if (msg) *msg = "认证成功";
        write_log("Auth success");
        return true;
    }

    if (ar.result == 0 && ar.ret_code == 2) {
        if (msg) *msg = ar.msg.empty() ? "终端IP已经在线" : ar.msg;
        write_log("Auth: already online (ret_code=2)");
        return true;
    }

    if (msg) *msg = error_msg;
    write_log("Auth failed: result=%d ret_code=%d msg=%s", ar.result, ar.ret_code, ar.msg.c_str());
    return false;
}

bool authenticate(std::string* msg = nullptr) {
    std::string ip;

    if (!g_config.fixed_ip.empty()) {
        ip = wstring_to_utf8(g_config.fixed_ip);
        write_log("Using fixed IP from config: %s", ip.c_str());
    } else {
        ip = get_local_ip();
    }

    std::string mac = get_local_mac();

    if (ip.empty()) {
        if (msg) *msg = "无法获取本机IP地址";
        write_log("Auth failed: cannot get local IP");
        return false;
    }

    // Step 1: Fetch login page to get PHPSESSID cookie
    std::string phpsessid;
    std::string login_page = fetch_login_page_and_get_cookie(phpsessid);
    // PHPSESSID is optional - we continue even without it

    // Step 2: Authenticate with configured IP
    bool success = do_auth_with_ip(ip, mac, phpsessid, msg);

    // Step 3: If using fixed_ip and auth failed, try auto-detected IP as fallback
    if (!success && !g_config.fixed_ip.empty()) {
        std::string auto_ip = get_local_ip();
        if (!auto_ip.empty() && auto_ip != ip) {
            write_log("Fixed IP auth failed, retrying with auto-detected IP: %s -> %s", ip.c_str(), auto_ip.c_str());
            success = do_auth_with_ip(auto_ip, mac, phpsessid, msg);
        }
    }

    return success;
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
#define ID_TRAY_RELOAD_CONFIG 1008
#define ID_TRAY_OP_CAMPUS 1101
#define ID_TRAY_OP_CMCC 1102
#define ID_TRAY_OP_UNICOM 1103
#define ID_TRAY_OP_TELECOM 1104

std::atomic<bool> g_running{true};
std::atomic<bool> g_guardian_active{false};
std::atomic<bool> g_guardian_enabled{false};
std::atomic<bool> g_auth_in_progress{false};
std::atomic<bool> g_autostart_enabled{false};
HANDLE g_guardian_thread = NULL;

enum class TrayIconState {
    Connected,
    Guardian,
    Disconnected,
    Reconnecting
};

std::atomic<TrayIconState> g_icon_state{TrayIconState::Connected};

NOTIFYICONDATAW g_nid = {};

HICON create_checkmark_icon() {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    HBRUSH hBrush = CreateSolidBrush(RGB(34, 197, 94));
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

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

HICON create_shield_icon() {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    HBRUSH hBrush = CreateSolidBrush(RGB(59, 130, 246));
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hFillBrush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, hFillBrush);

    POINT shield[] = {{8,2},{13,4},{13,8},{8,14},{3,8},{3,4}};
    Polygon(hdcMem, shield, 6);

    POINT innerShield[] = {{8,4},{11,5},{11,8},{8,12},{5,8},{5,5}};
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

HICON create_x_icon() {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hBmpOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    HBRUSH hBrush = CreateSolidBrush(RGB(239, 68, 68));
    RECT rect = {0, 0, size, size};
    FillRect(hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

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

// ============================================================================
// Windows Toast Notification (Action Center) via COM/WinRT
// ============================================================================
// WinRT HSTRING type — MinGW doesn't define it; we dynamically load all WinRT APIs
#ifndef _HSTRING_DEFINED_
typedef struct HSTRING__* HSTRING;
#endif

// AUMID must match a Start Menu shortcut for toast to appear in Action Center
static const wchar_t* TOAST_AUMID = L"CampusAuthGuardian";
static bool g_toast_available = false;
static bool g_toast_checked = false;

// COM initialization — done once at startup
struct ComInit {
    bool ok;
    ComInit() { ok = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)); }
    ~ComInit() { if (ok) CoUninitialize(); }
};
static ComInit g_com_init;

// IXmlDocumentIO — LoadXml method for WinRT XmlDocument
struct IXmlDocumentIO_Custom : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
};
// XML-escape text for toast content (handles < > & " ')
std::wstring xml_escape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'&': out += L"&amp;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool send_toast_impl(const wchar_t* title, const wchar_t* message) {
    // Dynamically load WinRT functions — works on x86/x64/ARM64
    HMODULE hRT = LoadLibraryExW(L"api-ms-win-core-winrt-l1-1-0.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hRT) hRT = LoadLibraryW(L"runtimeobject.dll");
    if (!hRT) return false;

    typedef HRESULT(WINAPI* RoGetActivationFactory_t)(HSTRING, REFIID, void**);
    typedef HRESULT(WINAPI* WindowsCreateString_t)(PCWSTR, UINT32, HSTRING*);
    typedef HRESULT(WINAPI* WindowsDeleteString_t)(HSTRING);
    typedef HRESULT(WINAPI* RoActivateInstance_t)(HSTRING, void**);
    typedef HRESULT(WINAPI* RoInitialize_t)(int);

    auto fnRoInit = (RoInitialize_t)GetProcAddress(hRT, "RoInitialize");
    auto fnCreateStr = (WindowsCreateString_t)GetProcAddress(hRT, "WindowsCreateString");
    auto fnDeleteStr = (WindowsDeleteString_t)GetProcAddress(hRT, "WindowsDeleteString");
    auto fnGetFactory = (RoGetActivationFactory_t)GetProcAddress(hRT, "RoGetActivationFactory");
    auto fnActivate = (RoActivateInstance_t)GetProcAddress(hRT, "RoActivateInstance");

    if (!fnCreateStr || !fnDeleteStr || !fnGetFactory || !fnActivate) return false;
    if (fnRoInit) fnRoInit(1);

    auto makeHs = [&](const wchar_t* s) -> HSTRING {
        HSTRING hs = nullptr;
        fnCreateStr(s, (UINT32)wcslen(s), &hs);
        return hs;
    };

    // 1. Create XmlDocument and load XML
    HSTRING hsXmlType = makeHs(L"Windows.Data.Xml.Dom.XmlDocument");
    void* pXmlInst = nullptr;
    HRESULT hr = fnActivate(hsXmlType, &pXmlInst);
    fnDeleteStr(hsXmlType);
    if (FAILED(hr) || !pXmlInst) { write_log("Toast: activate XmlDocument failed 0x%08lX", hr); return false; }

    // QI for IXmlDocumentIO to call LoadXml
    // IID {6CD3AC84-1848-457C-B1E2-BC44-DA81711F}
    IID iid_XmlDocIO = {0x6CD3AC84, 0x1848, 0x457C, {0xB1, 0xE2, 0xBC, 0x44, 0xDA, 0x81, 0x71, 0x1F}};
    IXmlDocumentIO_Custom* pXmlIO = nullptr;
    hr = ((IUnknown*)pXmlInst)->QueryInterface(iid_XmlDocIO, (void**)&pXmlIO);
    if (FAILED(hr) || !pXmlIO) {
        write_log("Toast: QI IXmlDocumentIO failed 0x%08lX", hr);
        ((IUnknown*)pXmlInst)->Release();
        return false;
    }

    std::wstring xml = L"<toast><visual><binding template='ToastGeneric'>"
        L"<text>" + xml_escape(title) + L"</text>"
        L"<text>" + xml_escape(message) + L"</text>"
        L"</binding></visual></toast>";

    HSTRING hsXml = makeHs(xml.c_str());
    hr = pXmlIO->LoadXml(hsXml);
    fnDeleteStr(hsXml);
    pXmlIO->Release(); // pXmlInst still alive
    if (FAILED(hr)) {
        write_log("Toast: LoadXml failed 0x%08lX", hr);
        ((IUnknown*)pXmlInst)->Release();
        return false;
    }

    // 2. Create ToastNotification from XmlDocument
    HSTRING hsToastType = makeHs(L"Windows.UI.Notifications.ToastNotification");
    // IID {04124B20-82C6-4229-B110-1E6E73134EB4} — IToastNotificationFactory
    IID iid_ToastFactory = {0x04124B20, 0x82C6, 0x4229, {0xB1, 0x10, 0x1E, 0x6E, 0x73, 0x13, 0x4E, 0xB4}};
    void* pToastFactoryRaw = nullptr;
    hr = fnGetFactory(hsToastType, iid_ToastFactory, &pToastFactoryRaw);
    fnDeleteStr(hsToastType);
    if (FAILED(hr) || !pToastFactoryRaw) {
        write_log("Toast: get ToastNotificationFactory failed 0x%08lX", hr);
        ((IUnknown*)pXmlInst)->Release();
        return false;
    }

    struct IToastNotificationFactory_C : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(void* content, void** notification) = 0;
    };
    auto pToastFactory = static_cast<IToastNotificationFactory_C*>(pToastFactoryRaw);

    void* pToastNotif = nullptr;
    hr = pToastFactory->CreateToastNotification(pXmlInst, &pToastNotif);
    pToastFactory->Release();
    ((IUnknown*)pXmlInst)->Release();
    if (FAILED(hr) || !pToastNotif) {
        write_log("Toast: CreateToastNotification failed 0x%08lX", hr);
        return false;
    }

    // 3. Get ToastNotifier and Show
    HSTRING hsManagerType = makeHs(L"Windows.UI.Notifications.ToastNotificationManager");
    // IID {53BFB467-74D5-4B0B-B224-97A4918D2414} — IToastNotificationManagerStatics
    IID iid_ManagerStatics = {0x53BFB467, 0x74D5, 0x4B0B, {0xB2, 0x24, 0x97, 0xA4, 0x91, 0x8D, 0x24, 0x14}};
    void* pManagerRaw = nullptr;
    hr = fnGetFactory(hsManagerType, iid_ManagerStatics, &pManagerRaw);
    fnDeleteStr(hsManagerType);
    if (FAILED(hr) || !pManagerRaw) {
        write_log("Toast: get ManagerStatics failed 0x%08lX", hr);
        ((IUnknown*)pToastNotif)->Release();
        return false;
    }

    struct IToastNotificationManagerStatics_C : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING id, void** notifier) = 0;
    };
    auto pManager = static_cast<IToastNotificationManagerStatics_C*>(pManagerRaw);

    HSTRING hsAumid = makeHs(TOAST_AUMID);
    void* pNotifierRaw = nullptr;
    hr = pManager->CreateToastNotifierWithId(hsAumid, &pNotifierRaw);
    fnDeleteStr(hsAumid);
    pManager->Release();
    if (FAILED(hr) || !pNotifierRaw) {
        write_log("Toast: CreateToastNotifier failed 0x%08lX", hr);
        ((IUnknown*)pToastNotif)->Release();
        return false;
    }

    struct IToastNotifier_C : IUnknown {
        virtual HRESULT STDMETHODCALLTYPE Show(void* notification) = 0;
        virtual HRESULT STDMETHODCALLTYPE Hide(void* notification) = 0;
    };
    auto pNotifier = static_cast<IToastNotifier_C*>(pNotifierRaw);

    hr = pNotifier->Show(pToastNotif);
    pNotifier->Release();
    ((IUnknown*)pToastNotif)->Release();

    if (FAILED(hr)) {
        write_log("Toast: Show failed 0x%08lX", hr);
        return false;
    }

    write_log("Toast notification sent successfully");
    return true;
}

// Create a Start Menu shortcut with AUMID so toast notifications are attributed
void ensure_toast_shortcut() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\";
        CreateDirectoryW(dir.c_str(), NULL);
        std::wstring lnk_path = dir + L"Campus Auth Guardian.lnk";

        if (GetFileAttributesW(lnk_path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path, MAX_PATH);

        IShellLinkW* pLink = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, (void**)&pLink))) {
            pLink->SetPath(exe_path);
            std::wstring work_dir = exe_path;
            size_t pos = work_dir.rfind(L'\\');
            if (pos != std::wstring::npos) work_dir = work_dir.substr(0, pos);
            pLink->SetWorkingDirectory(work_dir.c_str());

            // Set AUMID via IPropertyStore
            IPropertyStore* pPropStore = nullptr;
            if (SUCCEEDED(pLink->QueryInterface(IID_IPropertyStore, (void**)&pPropStore))) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_LPWSTR;
                size_t aumid_len = wcslen(TOAST_AUMID) + 1;
                pv.pwszVal = (wchar_t*)CoTaskMemAlloc(aumid_len * sizeof(wchar_t));
                if (pv.pwszVal) {
                    wcscpy(pv.pwszVal, TOAST_AUMID);
                    static const PROPERTYKEY PKEY_AppUserModel_ID = {
                        {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5
                    };
                    pPropStore->SetValue(PKEY_AppUserModel_ID, pv);
                    pPropStore->Commit();
                    PropVariantClear(&pv);
                }
                pPropStore->Release();
            }

            IPersistFile* pPersist = nullptr;
            if (SUCCEEDED(pLink->QueryInterface(IID_IPersistFile, (void**)&pPersist))) {
                pPersist->Save(lnk_path.c_str(), TRUE);
                pPersist->Release();
            }
            pLink->Release();
        }
        write_log("Created Start Menu shortcut for toast AUMID");
    }
}

void show_notification(const wchar_t* title, const wchar_t* message) {
    // Try Action Center toast first
    if (g_toast_available || !g_toast_checked) {
        if (send_toast_impl(title, message)) {
            g_toast_available = true;
            g_toast_checked = true;
            return;
        }
        g_toast_checked = true;
        g_toast_available = false;
        write_log("Toast unavailable, falling back to Shell_NotifyIconW");
    }

    // Fallback: tray balloon notification
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
        case TrayIconState::Connected: icon = create_checkmark_icon(); break;
        case TrayIconState::Guardian: icon = create_shield_icon(); break;
        case TrayIconState::Disconnected:
        case TrayIconState::Reconnecting:
        default: icon = create_x_icon(); break;
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

    int consecutive_failures = 0;

    while (g_running && g_guardian_enabled) {
        NetStatus net = check_internet_access();

        if (net == NetStatus::Connected) {
            consecutive_failures = 0;
            // Already connected, just wait
            if (g_icon_state != TrayIconState::Guardian) {
                g_icon_state = TrayIconState::Guardian;
                update_tray_icon();
                update_tray_tooltip(L"已连接 - 守护中");
            }
        } else if (net == NetStatus::CaptivePortal || net == NetStatus::Disconnected) {
            g_icon_state = TrayIconState::Disconnected;
            update_tray_icon();
            write_log("Network disconnected/captive portal, starting auth...");
            update_tray_tooltip(L"断开连接, 重新认证中...");

            std::string msg;
            int retry_count = 0;
            bool success = false;

            while (retry_count < g_config.max_retries && !success && g_running) {
                if (authenticate(&msg)) {
                    success = true;
                    consecutive_failures = 0;
                    write_log("Auth success!");
                    g_icon_state = TrayIconState::Guardian;
                    update_tray_icon();
                    update_tray_tooltip(L"已连接 - 守护中");
                    show_notification(L"Campus Guardian", L"网络重连成功!");
                } else {
                    retry_count++;
                    consecutive_failures++;
                    write_log("Auth failed (%d/%d): %s", retry_count, g_config.max_retries, msg.c_str());

                    // Show specific error notification for common issues
                    std::wstring wmsg = utf8_to_wstring(msg);
                    if (retry_count == 1) {
                        show_notification(L"认证失败", (L"错误: " + wmsg).c_str());
                    }

                    if (retry_count < g_config.max_retries) {
                        // Exponential backoff: base interval * 2^(min(failures-1, 4))
                        int backoff = g_config.retry_interval *
                            (1 << (std::min(consecutive_failures - 1, 4)));
                        if (backoff > 60) backoff = 60; // cap at 60s
                        write_log("Waiting %d seconds before retry...", backoff);
                        for (int i = 0; i < backoff && g_running && g_guardian_enabled; i++) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }
                    }
                }
            }

            if (!success && g_running) {
                write_log("All auth attempts failed, continuing monitoring");
                g_icon_state = TrayIconState::Disconnected;
                update_tray_icon();
                update_tray_tooltip(L"认证失败 - 继续监控中");
                show_notification(L"Campus Guardian - 认证失败",
                    L"多次认证失败，守护模式将继续监控网络并重试");
                // Don't stop guardian - keep monitoring, will retry next cycle
            }
        } else {
            // ServerError - network might be unstable
            write_log("Network check returned server error, retrying next cycle");
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
        update_tray_tooltip(L"守护模式已开启");
    } else {
        update_tray_tooltip(L"守护模式已关闭");
    }
}

bool is_autostart_enabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        bool exists = RegQueryValueExW(hKey, L"CampusAuthGuardian", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        return exists;
    }
    return false;
}

void set_autostart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exe_path[MAX_PATH];
            GetModuleFileNameW(NULL, exe_path, MAX_PATH);
            DWORD len = (DWORD)(wcslen(exe_path) + 1) * sizeof(wchar_t);
            RegSetValueExW(hKey, L"CampusAuthGuardian", 0, REG_SZ,
                (const BYTE*)exe_path, len);
            write_log("Autostart enabled");
        } else {
            RegDeleteValueW(hKey, L"CampusAuthGuardian");
            write_log("Autostart disabled");
        }
        RegCloseKey(hKey);
        g_autostart_enabled = enable;
    }
}

void open_login_web() {
    ShellExecuteW(NULL, L"open",
        L"http://10.10.102.50/",
        NULL, NULL, SW_SHOWNORMAL);
}

void open_config_file() {
    std::wstring config_path = utf8_to_wstring(get_config_path());
    ShellExecuteW(nullptr, L"open", config_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void reload_config() {
    std::string cfg_path = get_config_path();

    if (g_config.load(cfg_path)) {
        write_log("Config reloaded successfully");
        show_notification(L"Campus Guardian", L"配置已重新加载");
    } else {
        write_log("Config reload failed");
        show_notification(L"Campus Guardian", L"配置加载失败，请检查config.ini");
    }
}

// (get_config_path defined above near CONFIG_TEMPLATE)

void switch_operator(const std::wstring& new_op) {
    g_config.operator_type = new_op;
    g_config.build_user_account();
    g_config.save_operator(get_config_path());
    std::string op_name;
    if (new_op == L"campus") op_name = "校园网";
    else if (new_op == L"cmcc") op_name = "中国移动";
    else if (new_op == L"unicom") op_name = "中国联通";
    else if (new_op == L"telecom") op_name = "中国电信";
    else op_name = wstring_to_utf8(new_op);
    write_log("Operator switched to: %s (account: %s)", op_name.c_str(),
        wstring_to_utf8(g_config.user_account).c_str());
    std::wstring msg = L"运营商已切换为" + utf8_to_wstring(op_name);
    show_notification(L"Campus Guardian", msg.c_str());
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
    set_http_timeout(hInternet, 5000, 5000, 10000);

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

    AuthResult ar = parse_jsonp_response(response);
    if (ar.result == 1) {
        write_log("Logout success");
        return true;
    }

    std::string error_msg = get_auth_error_message(ar.result, ar.ret_code, ar.msg);
    write_log("Logout may have failed: %s", error_msg.c_str());
    return false;
}

void manual_auth() {
    if (g_auth_in_progress) return;
    g_auth_in_progress = true;

    write_log("Manual auth started");
    std::string msg;
    bool success = authenticate(&msg);

    if (success) {
        update_tray_tooltip(L"认证成功");
        show_notification(L"Campus Auth", L"登录成功!");
    } else {
        std::wstring wmsg = utf8_to_wstring(msg);
        update_tray_tooltip(L"认证失败");
        show_notification(L"Campus Auth", (L"登录失败: " + wmsg).c_str());
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

            wchar_t auth_text[64] = L"手动认证";
            if (g_auth_in_progress) wcscpy(auth_text, L"认证中...");

            AppendMenuW(hMenu, MF_STRING, ID_TRAY_AUTH, auth_text);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_WEB, L"打开登录页");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

            // Operator submenu
            HMENU hOpMenu = CreatePopupMenu();
            UINT op_flags;
            op_flags = MF_STRING | (g_config.operator_type == L"campus" ? MF_CHECKED : 0);
            AppendMenuW(hOpMenu, op_flags, ID_TRAY_OP_CAMPUS, L"校园网");
            op_flags = MF_STRING | (g_config.operator_type == L"cmcc" ? MF_CHECKED : 0);
            AppendMenuW(hOpMenu, op_flags, ID_TRAY_OP_CMCC, L"中国移动");
            op_flags = MF_STRING | (g_config.operator_type == L"unicom" ? MF_CHECKED : 0);
            AppendMenuW(hOpMenu, op_flags, ID_TRAY_OP_UNICOM, L"中国联通");
            op_flags = MF_STRING | (g_config.operator_type == L"telecom" ? MF_CHECKED : 0);
            AppendMenuW(hOpMenu, op_flags, ID_TRAY_OP_TELECOM, L"中国电信");
            AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hOpMenu, L"切换运营商");

            AppendMenuW(hMenu, MF_STRING | (g_guardian_enabled ? MF_CHECKED : 0), ID_TRAY_GUARDIAN, L"守护模式");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_LOGOUT, L"注销登录");
            AppendMenuW(hMenu, MF_STRING | (g_autostart_enabled ? MF_CHECKED : 0), ID_TRAY_AUTOSTART, L"开机自启");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_CONFIG, L"打开配置文件");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELOAD_CONFIG, L"重新加载配置");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

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
            set_autostart(!g_autostart_enabled);
            break;
        case ID_TRAY_OP_CAMPUS:
            switch_operator(L"campus");
            break;
        case ID_TRAY_OP_CMCC:
            switch_operator(L"cmcc");
            break;
        case ID_TRAY_OP_UNICOM:
            switch_operator(L"unicom");
            break;
        case ID_TRAY_OP_TELECOM:
            switch_operator(L"telecom");
            break;
        case ID_TRAY_OPEN_WEB:
            open_login_web();
            break;
        case ID_TRAY_LOGOUT:
            {
                std::thread t([]() {
                    if (logout()) {
                        update_tray_tooltip(L"已注销");
                        show_notification(L"Campus Guardian", L"注销成功!");
                    } else {
                        update_tray_tooltip(L"注销失败");
                        show_notification(L"Campus Guardian", L"注销可能失败，请查看日志");
                    }
                });
                t.detach();
            }
            break;
        case ID_TRAY_OPEN_CONFIG:
            open_config_file();
            break;
        case ID_TRAY_RELOAD_CONFIG:
            reload_config();
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

// ============================================================================
// Single instance check
HANDLE g_hInstanceMutex = NULL;

bool check_single_instance() {
    g_hInstanceMutex = CreateMutexW(NULL, TRUE, L"CampusAuthGuardian_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Use non-blocking notification — do not show modal dialog
        return false;
    }
    return true;
}

int main_loop() {
    // Single instance check
    if (!check_single_instance()) return 0;

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

    // Show balloon notification immediately so user sees something happened
    g_nid.uFlags = NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, L"Campus Guardian", sizeof(g_nid.szInfoTitle) / sizeof(wchar_t) - 1);
    wcsncpy(g_nid.szInfo, L"程序已启动，请查看系统托盘图标", sizeof(g_nid.szInfo) / sizeof(wchar_t) - 1);
    g_nid.uTimeout = 10000;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

    g_autostart_enabled = is_autostart_enabled();

    ensure_toast_shortcut();

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

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    write_log("Application exit");
    return 0;
}

int main(int argc, char* argv[]) {
    bool console_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--console") == 0) {
            console_mode = true;
        }
    }

    std::string cfg_path = get_config_path();

    if (!ensure_config_exists()) {
        if (console_mode) {
            printf("Error: Failed to create config.ini\n");
        } else {
            MessageBoxW(nullptr, L"Failed to create config.ini", L"Error", MB_ICONERROR);
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
        printf("Check log file for details: %s\\campus_auth.log\n", get_exe_dir().c_str());
        return success ? 0 : 1;
    }

    return main_loop();
}
