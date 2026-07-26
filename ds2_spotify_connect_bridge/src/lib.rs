mod audio;
mod config;
mod instance;
mod mdns;
mod metadata;
mod socket;
mod state;
mod zeroconf;

use audio::Ds2AudioSink;
use config::BridgeConfig;
use librespot::{
    connect::{ConnectConfig, Spirc},
    core::{Session, SessionConfig, authentication::Credentials, cache::Cache},
    playback::{mixer, mixer::MixerConfig, player::Player},
};
use state::SharedBridgeState;
use std::{
    error::Error,
    fs,
    io::Write,
    net::IpAddr,
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread,
};

const AUDIO_CACHE_LIMIT_BYTES: u64 = 1_000_000_000;
const CONNECT_PORT: u16 = 57_621;
static STARTED: AtomicBool = AtomicBool::new(false);

struct BridgeLog;

impl Write for BridgeLog {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        append_bridge_file("bridge.log", buffer);
        Ok(buffer.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

#[unsafe(no_mangle)]
pub extern "system" fn DS2SpotifyConnectBridgeStart() -> i32 {
    if STARTED.swap(true, Ordering::AcqRel) {
        return 1;
    }

    let started = thread::Builder::new()
        .name("DS2SpotifyConnect".to_owned())
        .spawn(|| {
            reset_bridge_logs();
            init_logging();
            write_status("bridge background thread started");
            let runtime = tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build();
            match runtime {
                Ok(runtime) => {
                    if let Err(error) = runtime.block_on(run_bridge()) {
                        write_status(&format!("bridge stopped: {error}"));
                        log::error!("Spotify Connect bridge stopped: {error}");
                    }
                }
                Err(error) => {
                    write_status(&format!("runtime startup failed: {error}"));
                    log::error!("Spotify Connect runtime startup failed: {error}");
                }
            }
            STARTED.store(false, Ordering::Release);
        })
        .is_ok();

    if started {
        1
    } else {
        STARTED.store(false, Ordering::Release);
        0
    }
}

async fn run_bridge() -> Result<(), Box<dyn Error>> {
    let _ = env_logger::try_init();
    let Some(_instance) = instance::ProcessInstance::acquire()? else {
        return Ok(());
    };
    let config = BridgeConfig::fixed()?;
    fs::create_dir_all(config.cache_dir.join("files"))?;
    let device_id = config.device_id()?;

    let state = SharedBridgeState::new();
    let session_config = SessionConfig {
        device_id,
        ..SessionConfig::default()
    };
    let connect_config = ConnectConfig {
        name: config.device_name,
        ..ConnectConfig::default()
    };
    let zeroconf_ips = zeroconf_ips();
    let (zeroconf_server, mut credentials_rx) = zeroconf::Server::start(
        zeroconf::Config::new(
            connect_config.name.clone(),
            session_config.device_id.clone(),
            session_config.client_id.clone(),
            connect_config.device_type,
            connect_config.is_group,
        ),
        CONNECT_PORT,
    )?;
    let _mdns_advertiser =
        mdns::Advertiser::start(&connect_config.name, &zeroconf_ips, CONNECT_PORT);

    let _zeroconf_server = zeroconf_server;
    write_status("discovery listener ready");
    log::info!("Spotify Connect device is ready: {}", connect_config.name);
    while let Some(credentials) = credentials_rx.recv().await {
        if let Err(error) = run_connect_session(
            credentials,
            &config.cache_dir,
            session_config.clone(),
            connect_config.clone(),
            &config.server_url,
            state.clone(),
        )
        .await
        {
            log::warn!("Spotify Connect session ended: {error}");
        }
    }
    Ok(())
}

async fn run_connect_session(
    credentials: Credentials,
    cache_dir: &std::path::Path,
    session_config: SessionConfig,
    connect_config: ConnectConfig,
    server_url: &str,
    state: std::sync::Arc<SharedBridgeState>,
) -> Result<(), Box<dyn Error>> {
    state.clear_snapshot();
    let (audio_tx, audio_rx) = mpsc::sync_channel(24);
    let files_dir = cache_dir.join("files");
    let cache = Cache::new(
        Some(cache_dir),
        Some(cache_dir),
        Some(&files_dir),
        Some(AUDIO_CACHE_LIMIT_BYTES),
    )?;
    let session = Session::new(session_config, Some(cache));
    let mixer = mixer::find(None)
        .ok_or_else(|| std::io::Error::other("missing librespot mixer"))?(
        MixerConfig::default()
    )?;
    let player = Player::new(
        Default::default(),
        session.clone(),
        mixer.get_soft_volume(),
        move || Box::new(Ds2AudioSink::new(audio_tx.clone())),
    );
    metadata::start(player.get_player_event_channel(), state.clone());

    let (spirc, task) = Spirc::new(connect_config, session, credentials, player, mixer).await?;
    state.set_spirc(spirc);
    let socket_worker = socket::start(server_url.to_owned(), audio_rx, state.clone());
    task.await;
    socket_worker.stop();
    state.clear_spirc();
    Ok(())
}

fn zeroconf_ips() -> Vec<IpAddr> {
    let Ok(interfaces) = if_addrs::get_if_addrs() else {
        return Vec::new();
    };
    interfaces
        .into_iter()
        .filter_map(|interface| match interface.ip() {
            IpAddr::V4(ip) if ip.is_private() => Some(IpAddr::V4(ip)),
            _ => None,
        })
        .collect()
}

fn init_logging() {
    let mut builder =
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"));
    builder.format_timestamp_millis();
    builder.target(env_logger::Target::Pipe(Box::new(BridgeLog)));
    let _ = builder.try_init();
}

fn reset_bridge_logs() {
    let Ok(config) = BridgeConfig::fixed() else {
        return;
    };
    if fs::create_dir_all(&config.cache_dir).is_err() {
        return;
    }
    for name in ["bridge-status.log", "bridge.log"] {
        let _ = fs::File::create(config.cache_dir.join(name));
    }
}

pub(crate) fn write_status(message: &str) {
    append_bridge_file("bridge-status.log", format!("{message}\n").as_bytes());
}

fn append_bridge_file(name: &str, contents: &[u8]) {
    let Ok(config) = BridgeConfig::fixed() else {
        return;
    };
    if fs::create_dir_all(&config.cache_dir).is_err() {
        return;
    }
    let path = config.cache_dir.join(name);
    let Ok(mut file) = fs::OpenOptions::new().create(true).append(true).open(path) else {
        return;
    };
    let _ = file.write_all(contents);
}
