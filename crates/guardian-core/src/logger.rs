//! 结构化日志：内存环形缓冲（供 UI 读取）+ 文件追加（512KB 轮转）。

use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{LazyLock, RwLock};
use std::time::{SystemTime, UNIX_EPOCH};

const MAX_MEMORY_LINES: usize = 1000;
const MAX_LOG_BYTES: u64 = 512 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Level {
    Info,
    Warn,
    Error,
}

impl Level {
    pub fn as_str(self) -> &'static str {
        match self {
            Level::Info => "INFO",
            Level::Warn => "WARN",
            Level::Error => "ERROR",
        }
    }
}

#[derive(Debug, Clone)]
pub struct LogLine {
    /// Unix 秒
    pub ts: u64,
    pub level: Level,
    pub text: String,
}

impl LogLine {
    pub fn formatted(&self) -> String {
        let (y, mo, d, h, mi, s) = format_cn_local(self.ts);
        format!("[{y:04}-{mo:02}-{d:02} {h:02}:{mi:02}:{s:02}] [{}] {}", self.level.as_str(), self.text)
    }
}

/// Unix 秒 → 东八区 (y,mo,d,h,mi,s)。纯整数算法，无外部依赖。
fn format_cn_local(unix: u64) -> (u64, u64, u64, u64, u64, u64) {
    let total = unix + 8 * 3600;
    let days = total / 86400;
    let rem = total % 86400;
    let (h, mi, s) = (rem / 3600, rem % 3600 / 60, rem % 60);
    // Howard Hinnant civil_from_days
    let z = days as i64 + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    (y as u64, m as u64, d as u64, h, mi, s)
}

struct LoggerInner {
    /// std RwLock；读多写少，poison 语义下用 unwrap_or 默认值即可恢复
    memory: RwLock<Vec<LogLine>>,
    file: RwLock<Option<File>>,
    path: RwLock<PathBuf>,
}

static LOGGER: LazyLock<LoggerInner> = LazyLock::new(|| LoggerInner {
    memory: RwLock::new(Vec::with_capacity(MAX_MEMORY_LINES)),
    file: RwLock::new(None),
    path: RwLock::new(PathBuf::from("campus_auth.log")),
});

fn memory() -> std::sync::RwLockWriteGuard<'static, Vec<LogLine>> {
    LOGGER.memory.write().unwrap_or_else(|e| e.into_inner())
}

/// 设置日志文件路径并打开句柄。幂等；重复调用切换目标文件。
pub fn init_file(path: &Path) {
    *LOGGER.path.write().unwrap_or_else(|e| e.into_inner()) = path.to_path_buf();
    *LOGGER.file.write().unwrap_or_else(|e| e.into_inner()) =
        OpenOptions::new().create(true).append(true).open(path).ok();
}

/// 记录一条日志。文件未初始化时仅写内存 + stderr（--console 模式）。
pub fn log(level: Level, text: impl Into<String>) {
    let line = LogLine {
        ts: SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0),
        level,
        text: text.into(),
    };

    {
        let mut mem = memory();
        let len = mem.len();
        if len >= MAX_MEMORY_LINES {
            mem.drain(..len - MAX_MEMORY_LINES + 1);
        }
        mem.push(line.clone());
    }
    let mut file_slot = LOGGER.file.write().unwrap_or_else(|e| e.into_inner());
    match file_slot.as_mut() {
        None => {
            // 未 init：console 模式
            drop(file_slot);
            eprintln!("{}", line.formatted());
        }
        Some(f) => {
            let _ = writeln!(f, "{}", line.formatted());
            let size = f.metadata().map(|m| m.len()).unwrap_or(0);
            if size >= MAX_LOG_BYTES {
                let path = LOGGER.path.read().unwrap_or_else(|e| e.into_inner()).clone();
                let rotated = path.with_extension("log.1");
                let _ = std::fs::remove_file(&rotated);
                // Windows rename 需要关闭句柄
                *file_slot = None;
                drop(file_slot);
                if std::fs::rename(&path, &rotated).is_ok() {
                    *LOGGER.file.write().unwrap_or_else(|e| e.into_inner()) =
                        OpenOptions::new().create(true).append(true).open(&path).ok();
                }
            }
        }
    }
}

/// 读取内存日志（最新在后）。
pub fn recent() -> Vec<LogLine> {
    LOGGER
        .memory
        .read()
        .unwrap_or_else(|e| e.into_inner())
        .clone()
}

/// 便捷宏：info 级别。
#[macro_export]
macro_rules! log_info {
    ($($arg:tt)*) => { $crate::logger::log($crate::logger::Level::Info, format!($($arg)*)) };
}

/// 便捷宏：warn 级别。
#[macro_export]
macro_rules! log_warn {
    ($($arg:tt)*) => { $crate::logger::log($crate::logger::Level::Warn, format!($($arg)*)) };
}

/// 便捷宏：error 级别。
#[macro_export]
macro_rules! log_error {
    ($($arg:tt)*) => { $crate::logger::log($crate::logger::Level::Error, format!($($arg)*)) };
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cn_local_time_conversion() {
        // 2026-09-01 00:00:00 UTC = 08:00 CST（时间戳 1788220800）
        let unix: u64 = 1788220800;
        let (y, mo, d, h, mi, s) = format_cn_local(unix);
        assert_eq!((y, mo, d, h, mi, s), (2026, 9, 1, 8, 0, 0));
    }

    #[test]
    fn log_format() {
        let line = LogLine { ts: 1788220800, level: Level::Info, text: "hello".into() };
        assert_eq!(line.formatted(), "[2026-09-01 08:00:00] [INFO] hello");
    }
}
