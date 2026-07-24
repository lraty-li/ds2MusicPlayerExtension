use std::{env, io, path::PathBuf};

pub struct BridgeConfig {
    pub cache_dir: PathBuf,
    pub device_name: String,
    pub server_url: String,
}

impl BridgeConfig {
    pub fn from_args() -> io::Result<Self> {
        let mut cache_dir = default_cache_dir()?;
        let mut device_name = String::from("Death Stranding 2");
        let mut server_url = String::from("ws://127.0.0.1:47832");
        let mut args = env::args().skip(1);

        while let Some(flag) = args.next() {
            let value = match flag.as_str() {
                "--cache" => args.next(),
                "--name" => args.next(),
                "--server" => args.next(),
                "--help" | "-h" => {
                    print_usage();
                    std::process::exit(0);
                }
                _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "unknown argument")),
            };

            let value = value.ok_or_else(|| {
                io::Error::new(io::ErrorKind::InvalidInput, "missing argument value")
            })?;
            match flag.as_str() {
                "--cache" => cache_dir = PathBuf::from(value),
                "--name" => device_name = value,
                "--server" => server_url = value,
                _ => unreachable!(),
            }
        }

        if device_name.trim().is_empty() || server_url.trim().is_empty() {
            return Err(io::Error::new(io::ErrorKind::InvalidInput, "empty configuration"));
        }

        Ok(Self { cache_dir, device_name, server_url })
    }
}

fn default_cache_dir() -> io::Result<PathBuf> {
    let base = env::var_os("LOCALAPPDATA").ok_or_else(|| {
        io::Error::new(io::ErrorKind::NotFound, "LOCALAPPDATA is unavailable")
    })?;
    Ok(PathBuf::from(base).join("DS2MusicPlayer").join("SpotifyConnect"))
}

fn print_usage() {
    println!("DS2SpotifyConnectBridge [--cache PATH] [--name NAME] [--server WS_URL]");
}
