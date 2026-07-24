use crate::state::SharedBridgeState;
use std::{
    io::ErrorKind,
    sync::{Arc, atomic::{AtomicBool, Ordering}, mpsc::Receiver},
    thread,
    time::Duration,
};
use tungstenite::{
    Error, Message, WebSocket, connect,
    stream::MaybeTlsStream,
};

pub struct SocketWorker(Arc<AtomicBool>);

impl SocketWorker {
    pub fn stop(&self) {
        self.0.store(true, Ordering::Release);
    }
}

pub fn start(server_url: String, audio_rx: Receiver<Vec<u8>>, state: Arc<SharedBridgeState>) -> SocketWorker {
    let stop = Arc::new(AtomicBool::new(false));
    let worker_stop = stop.clone();
    thread::spawn(move || while !worker_stop.load(Ordering::Acquire) {
        match connect(server_url.as_str()) {
            Ok((socket, _)) => run_connection(socket, &audio_rx, &state, &worker_stop),
            Err(error) => log::debug!("game stream is unavailable: {error}"),
        }
        thread::sleep(Duration::from_secs(1));
    });
    SocketWorker(stop)
}

fn run_connection(
    mut socket: WebSocket<MaybeTlsStream<std::net::TcpStream>>,
    audio_rx: &Receiver<Vec<u8>>,
    state: &SharedBridgeState,
    stop: &AtomicBool,
) {
    if let MaybeTlsStream::Plain(stream) = socket.get_mut() {
        if stream.set_read_timeout(Some(Duration::from_millis(10))).is_err() {
            return;
        }
    }

    log::info!("connected to game audio stream");
    let mut metadata_generation = None;
    let mut jacket_generation = None;
    while !stop.load(Ordering::Acquire) {
        if !send_snapshot(&mut socket, state, &mut metadata_generation, &mut jacket_generation) {
            return;
        }
        if !send_audio(&mut socket, audio_rx) {
            return;
        }
        match socket.read() {
            Ok(Message::Text(text)) => apply_control(&text, state),
            Ok(Message::Close(_)) => return,
            Ok(_) => {}
            Err(Error::Io(error)) if is_timeout(&error) => {}
            Err(Error::ConnectionClosed | Error::AlreadyClosed) => return,
            Err(error) => {
                log::debug!("game stream read failed: {error}");
                return;
            }
        }
    }
}

fn send_snapshot(
    socket: &mut WebSocket<MaybeTlsStream<std::net::TcpStream>>,
    state: &SharedBridgeState,
    metadata_generation: &mut Option<u64>,
    jacket_generation: &mut Option<u64>,
) -> bool {
    let Some(snapshot) = state.snapshot() else {
        return true;
    };
    if *metadata_generation != Some(snapshot.generation) {
        if socket.send(Message::Text(snapshot.metadata_json.into())).is_err() {
            return false;
        }
        *metadata_generation = Some(snapshot.generation);
    }
    if snapshot.jacket_json.is_some() && *jacket_generation != Some(snapshot.generation) {
        if socket.send(Message::Text(snapshot.jacket_json.unwrap().into())).is_err() {
            return false;
        }
        *jacket_generation = Some(snapshot.generation);
    }
    true
}

fn send_audio(socket: &mut WebSocket<MaybeTlsStream<std::net::TcpStream>>, audio_rx: &Receiver<Vec<u8>>) -> bool {
    while let Ok(packet) = audio_rx.try_recv() {
        if socket.send(Message::Binary(packet.into())).is_err() {
            return false;
        }
    }
    true
}

fn apply_control(text: &str, state: &SharedBridgeState) {
    let command = serde_json::from_str::<serde_json::Value>(text)
        .ok()
        .and_then(|value| value.get("command")?.as_str().map(str::to_owned));
    match command.as_deref() {
        Some("pause") => state.pause(),
        Some("resume") => state.resume(),
        _ => {}
    }
}

fn is_timeout(error: &std::io::Error) -> bool {
    matches!(error.kind(), ErrorKind::TimedOut | ErrorKind::WouldBlock)
}
