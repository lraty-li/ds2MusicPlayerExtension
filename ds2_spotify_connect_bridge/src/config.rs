use std::{env, io, path::PathBuf};

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
}

fn default_cache_dir() -> io::Result<PathBuf> {
    let base = env::var_os("LOCALAPPDATA").ok_or_else(|| {
        io::Error::new(io::ErrorKind::NotFound, "LOCALAPPDATA is unavailable")
    })?;
    Ok(PathBuf::from(base).join("DS2MusicPlayer").join("SpotifyConnect"))
}
