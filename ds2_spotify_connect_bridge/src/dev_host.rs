use std::os::windows::ffi::OsStrExt;
use std::{
    env,
    io::{self, Read},
    mem,
    net::TcpListener,
    path::PathBuf,
    thread,
};
use tungstenite::{Message, accept};
use windows_sys::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};

type StartBridge = unsafe extern "system" fn() -> i32;

fn main() -> io::Result<()> {
    let _stream_probe = start_stream_probe()?;
    let dll_path = bridge_path()?;
    let wide_path: Vec<u16> = dll_path
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let module = unsafe { LoadLibraryW(wide_path.as_ptr()) };
    if module.is_null() {
        return Err(io::Error::last_os_error());
    }
    let proc = unsafe { GetProcAddress(module, c"DS2SpotifyConnectBridgeStart".as_ptr().cast()) };
    let Some(proc) = proc else {
        return Err(io::Error::last_os_error());
    };
    let start: StartBridge = unsafe { mem::transmute(proc) };
    if unsafe { start() } == 0 {
        return Err(io::Error::other("Bridge refused to start"));
    }

    println!("DS2 Spotify Connect development host is running.");
    println!("Open Spotify's device picker and play an allowed track.");
    println!("This host validates bridge metadata and PCM packets without starting the game.");
    println!("Press Enter here to stop.");
    let _ = io::stdin().read(&mut [0u8]);
    Ok(())
}

fn start_stream_probe() -> io::Result<thread::JoinHandle<()>> {
    let listener = TcpListener::bind("127.0.0.1:47832")?;
    thread::Builder::new()
        .name("DS2SpotifyStreamProbe".to_owned())
        .spawn(move || {
            let Ok((stream, peer)) = listener.accept() else {
                return;
            };
            let Ok(mut socket) = accept(stream) else {
                return;
            };
            println!("Bridge stream probe connected: {peer}");
            let mut expected_sequence = None;
            while let Ok(message) = socket.read() {
                match message {
                    Message::Text(text) => inspect_metadata(&text),
                    Message::Binary(packet) => inspect_audio(&packet, &mut expected_sequence),
                    Message::Close(_) => break,
                    _ => {}
                }
            }
            println!("Bridge stream probe disconnected.");
        })
}

fn inspect_metadata(text: &str) {
    let kind = serde_json::from_str::<serde_json::Value>(text)
        .ok()
        .and_then(|value| {
            value
                .get("type")
                .and_then(|value| value.as_str())
                .map(str::to_owned)
        })
        .unwrap_or_else(|| String::from("unknown"));
    println!("Bridge stream metadata: {kind}");
}

fn inspect_audio(packet: &[u8], expected_sequence: &mut Option<u64>) {
    if packet.len() < 32 {
        println!("Bridge stream invalid PCM packet: {} bytes", packet.len());
        return;
    }
    let magic = u32::from_le_bytes(packet[0..4].try_into().unwrap());
    let channels = u16::from_le_bytes(packet[6..8].try_into().unwrap());
    let sample_rate = u32::from_le_bytes(packet[8..12].try_into().unwrap());
    let frames = u32::from_le_bytes(packet[12..16].try_into().unwrap());
    let sequence = u64::from_le_bytes(packet[16..24].try_into().unwrap());
    let payload_bytes = u32::from_le_bytes(packet[24..28].try_into().unwrap()) as usize;
    if magic != 0x4453_3241 || packet.len() != 32 + payload_bytes {
        println!("Bridge stream invalid PCM header at sequence {sequence}");
        return;
    }
    if let Some(expected) = *expected_sequence
        && sequence != expected
    {
        println!("Bridge stream packet gap: expected {expected}, got {sequence}");
    }
    *expected_sequence = Some(sequence + 1);
    if sequence % 100 == 0 {
        println!(
            "Bridge stream PCM: sequence={sequence} channels={channels} rate={sample_rate} frames={frames}"
        );
    }
}

fn bridge_path() -> io::Result<PathBuf> {
    let executable = env::current_exe()?;
    let directory = executable
        .parent()
        .ok_or_else(|| io::Error::other("Development host has no parent directory"))?;
    Ok(directory.join("DS2SpotifyConnectBridge.dll"))
}
