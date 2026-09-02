//! 校园网 ePortal 认证内核。
//!
//! 纯逻辑层，无 UI 依赖；通过 `ffi` 模块向 C++/WinUI3 壳暴露 C ABI。

pub mod auth;
pub mod config;
pub mod ffi;
pub mod guardian;
pub mod ipdetect;
pub mod logger;
pub mod netcheck;

pub use guardian::{Guardian, GuardianEvent, GuardianState};
