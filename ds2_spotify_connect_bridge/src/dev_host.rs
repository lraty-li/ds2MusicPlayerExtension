use std::{
    env,
    io::{self, Read},
    mem,
    path::PathBuf,
};
use std::os::windows::ffi::OsStrExt;
use windows_sys::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};

type StartBridge = unsafe extern "system" fn() -> i32;

fn main() -> io::Result<()> {
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
    println!("Open Spotify's device picker. Press Enter here to stop.");
    let _ = io::stdin().read(&mut [0u8]);
    Ok(())
}

fn bridge_path() -> io::Result<PathBuf> {
    let executable = env::current_exe()?;
    let directory = executable
        .parent()
        .ok_or_else(|| io::Error::other("Development host has no parent directory"))?;
    Ok(directory.join("DS2SpotifyConnectBridge.dll"))
}
