mod audio;
mod config;
mod instance;
mod mdns;
mod metadata;
mod socket;
mod state;

use audio::Ds2AudioSink;
use config::BridgeConfig;
use futures_util::StreamExt;
use librespot::{
    connect::{ConnectConfig, Spirc},
    core::{Session, SessionConfig, authentication::Credentials, cache::Cache},
    discovery::{Discovery, find as find_discovery},
    playback::{mixer, mixer::MixerConfig, player::Player},
};
use sha1::{Digest, Sha1};
use std::{error::Error, fs, net::IpAddr, sync::mpsc};
use state::SharedBridgeState;

const CONNECT_PORT: u16 = 57_621;

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), Box<dyn Error>> {
    env_logger::init();
    let Some(_instance) = instance::ProcessInstance::acquire()? else {
        return Ok(());
    };
    let config = BridgeConfig::from_args()?;
    fs::create_dir_all(config.cache_dir.join("files"))?;

    let state = SharedBridgeState::new();

    let session_config = SessionConfig {
        device_id: device_id(&config.device_name),
        ..SessionConfig::default()
    };
    let connect_config = ConnectConfig {
        name: config.device_name,
        ..ConnectConfig::default()
    };
    let zeroconf_ips = zeroconf_ips();
    let discovery_backend = find_discovery(None)?;
    let mut discovery = Discovery::builder(
        session_config.device_id.clone(),
        session_config.client_id.clone(),
    )
    .name(connect_config.name.clone())
    .device_type(connect_config.device_type)
    .is_group(connect_config.is_group)
    .port(CONNECT_PORT)
    .zeroconf_ip(zeroconf_ips.clone())
    .zeroconf_backend(discovery_backend)
    .launch()?;
    let _mdns_advertiser = mdns::Advertiser::start(
        &connect_config.name,
        &zeroconf_ips,
        CONNECT_PORT,
    );

    log::info!("Spotify Connect device is ready: {}", connect_config.name);
    while let Some(credentials) = discovery.next().await {
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
    let cache = Cache::new(Some(cache_dir), Some(cache_dir), Some(&files_dir), None)?;
    let session = Session::new(session_config, Some(cache));
    let mixer = mixer::find(None)
        .ok_or_else(|| std::io::Error::other("missing librespot mixer"))?(MixerConfig::default())?;
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

fn device_id(name: &str) -> String {
    let digest = Sha1::digest(name.as_bytes());
    digest.iter().map(|byte| format!("{byte:02x}")).collect()
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
