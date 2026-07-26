use rand::RngCore;
use std::{env, fs, io, path::PathBuf};

const DEVICE_ID_BYTES: usize = 20;
const DEVICE_ID_FILE: &str = "device-id";

pub struct BridgeConfig {
    pub cache_dir: PathBuf,
    pub device_name: String,
    pub server_url: String,
}

impl BridgeConfig {
    pub fn fixed() -> io::Result<Self> {
        Ok(Self {
            cache_dir: default_cache_dir()?,
            device_name: String::from("Death Stranding 2"),
            server_url: String::from("ws://127.0.0.1:47832"),
        })
    }

    pub fn device_id(&self) -> io::Result<String> {
        let path = self.cache_dir.join(DEVICE_ID_FILE);
        if let Ok(value) = fs::read_to_string(&path) {
            let value = value.trim();
            if is_device_id(value) {
                return Ok(value.to_owned());
            }
        }

        fs::create_dir_all(&self.cache_dir)?;
        let mut bytes = [0u8; DEVICE_ID_BYTES];
        rand::rng().fill_bytes(&mut bytes);
        let device_id = bytes.iter().map(|byte| format!("{byte:02x}")).collect();
        fs::write(path, &device_id)?;
        Ok(device_id)
    }
}

fn default_cache_dir() -> io::Result<PathBuf> {
    let base = env::var_os("LOCALAPPDATA")
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "LOCALAPPDATA is unavailable"))?;
    Ok(PathBuf::from(base)
        .join("DS2MusicPlayer")
        .join("SpotifyConnect"))
}

fn is_device_id(value: &str) -> bool {
    value.len() == DEVICE_ID_BYTES * 2 && value.bytes().all(|byte| byte.is_ascii_hexdigit())
}
