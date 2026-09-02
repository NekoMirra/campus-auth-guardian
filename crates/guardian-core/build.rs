fn main() {
    if std::env::var("CARGO_CFG_WINDOWS").is_ok() {
        // GetAdaptersAddresses
        println!("cargo:rustc-link-lib=iphlpapi");
        // WS2_32 sockaddr family 常量通过 windows-sys；运行时不直接调用 ws2_32 函数，
        // 但 sockaddr 结构引用需要 ws2_32 符号解析（inet 相关未用则无需）。
        println!("cargo:rustc-link-lib=ws2_32");
    }
}
