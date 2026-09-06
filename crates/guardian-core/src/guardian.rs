//! 守护循环：独立线程周期检测网络，断网时指数退避重试认证。
//!
//! 并发语义：`Guardian` 可安全跨线程共享（内部 std 锁保护）；

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use std::thread::{JoinHandle};

use crate::auth::{self, AuthOutcome};
use crate::config::Config;
use crate::netcheck::{self, NetStatus};
use crate::{log_error, log_info, log_warn};

/// 守护循环运行状态（对 UI 展示）。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GuardianState {
    Stopped,
    Monitoring,
    Authenticating,
}

/// 发给 UI 的事件。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GuardianEvent {
    State(GuardianState),
    NetStatus(NetStatus),
    AuthResult(AuthOutcome),
    ConfigReloaded,
}

struct Inner {
    running: AtomicBool,
    /// 手动触发一次认证的旗标
    manual_kick: AtomicBool,
    state: Mutex<GuardianState>,
    cfg: Mutex<Config>,
    cfg_path: Mutex<std::path::PathBuf>,
}

pub struct Guardian {
    inner: Arc<Inner>,
    /// 事件接收端（clone 给 UI）
    pub events: crossbeam_channel::Receiver<GuardianEvent>,
    events_tx: crossbeam_channel::Sender<GuardianEvent>,
    handle: Mutex<Option<JoinHandle<()>>>,
}

impl Guardian {
    /// 创建守护器并启动工作线程。`cfg` 为初始配置。
    pub fn start(cfg: Config, cfg_path: std::path::PathBuf) -> Self {
        let (tx, rx) = crossbeam_channel::unbounded();
        let inner = Arc::new(Inner {
            running: AtomicBool::new(false),
            manual_kick: AtomicBool::new(false),
            state: std::sync::Mutex::new(GuardianState::Stopped),
            cfg: std::sync::Mutex::new(cfg),
            cfg_path: std::sync::Mutex::new(cfg_path),
        });
        let g = Self {
            inner,
            events: rx,
            events_tx: tx,
            handle: std::sync::Mutex::new(None),
        };
        g.spawn_thread();
        g
    }

    fn spawn_thread(&self) {
        let inner = Arc::clone(&self.inner);
        let tx = self.events_tx.clone();
        let handle = std::thread::Builder::new()
            .name("guardian-loop".into())
            .spawn(move || run_loop(inner, tx))
            .expect("spawn guardian thread");
        *self.handle.lock().unwrap_or_else(|e| e.into_inner()) = Some(handle);
    }

    /// 开启守护模式。
    pub fn enable(&self) {
        self.inner.running.store(true, Ordering::SeqCst);
        self.set_state(GuardianState::Monitoring);
        log_info!("Guardian enabled");
    }

    /// 关闭守护模式。
    pub fn disable(&self) {
        self.inner.running.store(false, Ordering::SeqCst);
        self.set_state(GuardianState::Stopped);
        log_info!("Guardian disabled");
    }

    pub fn is_running(&self) -> bool {
        self.inner.running.load(Ordering::SeqCst)
    }

    pub fn state(&self) -> GuardianState {
        *self.inner.state.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// 手动触发一次认证（无论守护开关）。
    pub fn kick(&self) {
        self.inner.manual_kick.store(true, Ordering::SeqCst);
    }

    /// 热重载配置文件。
    pub fn reload_config(&self) -> std::io::Result<()> {
        let path = self.inner.cfg_path.lock().unwrap_or_else(|e| e.into_inner()).clone();
        let cfg = Config::load_or_create(&path)?;
        *self.inner.cfg.lock().unwrap_or_else(|e| e.into_inner()) = cfg;
        self.events_tx.send(GuardianEvent::ConfigReloaded).ok();
        log_info!("配置已重载");
        Ok(())
    }

    /// 直接替换配置（UI 保存后调用）并持久化。
    pub fn update_config(&self, cfg: Config) -> std::io::Result<()> {
        let path = self.inner.cfg_path.lock().unwrap_or_else(|e| e.into_inner()).clone();
        cfg.save(&path)?;
        *self.inner.cfg.lock().unwrap_or_else(|e| e.into_inner()) = cfg;
        self.events_tx.send(GuardianEvent::ConfigReloaded).ok();
        log_info!("配置已保存");
        Ok(())
    }

    /// 当前配置快照。
    pub fn config(&self) -> Config {
        self.inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    /// 同步执行一次认证（阻塞调用线程）。
    pub fn auth_once(&self) -> AuthOutcome {
        let cfg = self.inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
        self.set_state(GuardianState::Authenticating);
        let r = auth::authenticate(&cfg);
        self.events_tx.send(GuardianEvent::AuthResult(r.clone())).ok();
        self.set_state(if self.is_running() {
            GuardianState::Monitoring
        } else {
            GuardianState::Stopped
        });
        r
    }

    /// 停止线程（drop 前调用；也可直接 drop）。
    pub fn stop(&self) {
        self.inner.running.store(false, Ordering::SeqCst);
        if let Some(h) = self.handle.lock().unwrap_or_else(|e| e.into_inner()).take() {
            // 循环每秒检查一次运行旗标，最多等 2 秒
            let _ = h.join();
        }
    }

    fn set_state(&self, s: GuardianState) {
        *self.inner.state.lock().unwrap_or_else(|e| e.into_inner()) = s;
        self.events_tx.send(GuardianEvent::State(s)).ok();
    }
}

impl Drop for Guardian {
    fn drop(&mut self) {
        self.stop();
    }
}

fn run_loop(inner: Arc<Inner>, tx: crossbeam_channel::Sender<GuardianEvent>) {
    // 主循环：以 1s 粒度轮询旗标，避免长 sleep 期间无法响应退出/手动触发
    let mut next_check = std::time::Instant::now();
    let mut consecutive_failures: u32 = 0;

    loop {
        if inner.manual_kick.swap(false, Ordering::SeqCst) {
            do_auth(&inner, &tx, None);
            consecutive_failures = 0;
            next_check = std::time::Instant::now() + inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).check_interval;
        }

        if !inner.running.load(Ordering::SeqCst) {
            // 守护关闭：不发认证，但仍周期检测网络，驱动 UI 状态卡
            let now = std::time::Instant::now();
            if now >= next_check {
                let cfg = inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
                let status = netcheck::check(&cfg.check_url, Duration::from_secs(8));
                tx.send(GuardianEvent::NetStatus(status)).ok();
                next_check = now + cfg.check_interval;
            }
            std::thread::sleep(Duration::from_millis(500));
            continue;
        }

        let now = std::time::Instant::now();
        if now >= next_check {
            let cfg = inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
            let status = netcheck::check(&cfg.check_url, Duration::from_secs(8));

            match &status {
                NetStatus::Connected => {
                    // 确认机制：认证刚成功后 DNS/路由可能抖动，连续 2 次通过才清零
                    consecutive_failures = consecutive_failures.saturating_sub(1);
                    next_check = now + cfg.check_interval;
                }
                NetStatus::DnsPending => {
                    // DNS 暂未就绪：短延迟快速复检（5s），不计失败
                    tx.send(GuardianEvent::NetStatus(NetStatus::DnsPending)).ok();
                    log_info!("DNS 暂未就绪，5s 后复检");
                    next_check = std::time::Instant::now() + Duration::from_secs(5);
                    std::thread::sleep(Duration::from_millis(500));
                    continue;
                }
                _ => {}
            }

            tx.send(GuardianEvent::NetStatus(status.clone())).ok();
            match status {
                NetStatus::Connected => {
                    consecutive_failures = 0;
                    next_check = now + cfg.check_interval;
                }
                NetStatus::CaptivePortal { redirect } => {
                    log_warn!("检测到 captive portal: {redirect}");
                    // 提取 AC 参数（wlanacip/wlanacname），认证请求回填
                    let (ac_ip, ac_name, portal_user_ip) = crate::netcheck::extract_ac_params(&redirect);
                    log_info!("AC 参数: ip={ac_ip} name={ac_name} user_ip={portal_user_ip}");
                    consecutive_failures = run_retry_burst(&inner, &tx, Some((ac_ip.as_str(), ac_name.as_str(), portal_user_ip.as_str())));
                    // 失败爆发后指数退避：2^n * retry_interval，封顶 10 分钟
                    let backoff = backoff_delay(&cfg, consecutive_failures);
                    next_check = std::time::Instant::now() + backoff;
                }
                NetStatus::DnsPending => unreachable!(),
                NetStatus::Disconnected { reason } => {
                    log_warn!("网络不可达: {reason}");
                    consecutive_failures += 1;
                    let backoff = backoff_delay(&cfg, consecutive_failures);
                    next_check = std::time::Instant::now() + backoff;
                }
            }
        }
        std::thread::sleep(Duration::from_millis(1000));
    }
}

/// 指数退避：retry_interval * 2^(failures-1)，封顶 600s。
fn backoff_delay(cfg: &Config, failures: u32) -> Duration {
    if failures == 0 {
        return cfg.check_interval;
    }
    let exp = failures.saturating_sub(1).min(10);
    let secs = cfg.retry_interval.as_secs().saturating_mul(1u64 << exp).min(600);
    Duration::from_secs(secs)
}

/// 一轮认证爆发：最多 cfg.max_retries 次，每次间隔 retry_interval。
/// 返回仍剩余的连续失败数（0 = 成功）。
fn run_retry_burst(
    inner: &Arc<Inner>,
    tx: &crossbeam_channel::Sender<GuardianEvent>,
    ac: Option<(&str, &str, &str)>,
) -> u32 {
    let cfg = inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
    for attempt in 1..=cfg.max_retries {
        if !inner.running.load(Ordering::SeqCst) {
            return 0;
        }
        let outcome = do_auth(inner, tx, ac);
        if outcome.is_ok() {
            return 0;
        }
        log_warn!("第 {attempt}/{} 次认证失败", cfg.max_retries);
        if attempt < cfg.max_retries {
            std::thread::sleep(cfg.retry_interval);
        }
    }
    log_error!("连续 {} 次认证失败，进入退避等待", cfg.max_retries);
    cfg.max_retries
}

fn do_auth(
    inner: &Arc<Inner>,
    tx: &crossbeam_channel::Sender<GuardianEvent>,
    ac: Option<(&str, &str, &str)>,
) -> AuthOutcome {
    let cfg = inner.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
    inner_state(inner, GuardianState::Authenticating, tx);
    let outcome = auth::authenticate_with_ac(&cfg, ac);
    tx.send(GuardianEvent::AuthResult(outcome.clone())).ok();
    inner_state(inner, GuardianState::Monitoring, tx);
    match &outcome {
        AuthOutcome::Success => log_info!("认证成功"),
        AuthOutcome::AlreadyOnline => log_info!("已在线，无需认证"),
        AuthOutcome::Failed { msg } => log_error!("认证失败: {msg}"),
        AuthOutcome::NetworkError { msg } => log_error!("认证网络错误: {msg}"),
    }
    outcome
}

fn inner_state(inner: &Arc<Inner>, s: GuardianState, tx: &crossbeam_channel::Sender<GuardianEvent>) {
    *inner.state.lock().unwrap_or_else(|e| e.into_inner()) = s;
    tx.send(GuardianEvent::State(s)).ok();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn backoff_grows_and_caps() {
        let mut cfg = Config::default();
        cfg.retry_interval = Duration::from_secs(10);
        assert_eq!(backoff_delay(&cfg, 0), Duration::from_secs(30)); // check_interval
        assert_eq!(backoff_delay(&cfg, 1), Duration::from_secs(10));
        assert_eq!(backoff_delay(&cfg, 2), Duration::from_secs(20));
        assert_eq!(backoff_delay(&cfg, 3), Duration::from_secs(40));
        assert_eq!(backoff_delay(&cfg, 8), Duration::from_secs(600)); // 封顶
    }
}
