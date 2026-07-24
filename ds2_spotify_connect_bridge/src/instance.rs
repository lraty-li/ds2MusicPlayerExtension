use std::io;
use windows_sys::Win32::{
    Foundation::{CloseHandle, ERROR_ALREADY_EXISTS, GetLastError, HANDLE},
    System::Threading::CreateMutexW,
};

pub struct ProcessInstance(HANDLE);

impl ProcessInstance {
    pub fn acquire() -> io::Result<Option<Self>> {
        let name: Vec<u16> = "Local\\DS2MusicPlayerSpotifyConnectBridge"
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
        let handle = unsafe { CreateMutexW(std::ptr::null(), 0, name.as_ptr()) };
        if handle.is_null() {
            return Err(io::Error::last_os_error());
        }
        if unsafe { GetLastError() } == ERROR_ALREADY_EXISTS {
            unsafe { CloseHandle(handle) };
            return Ok(None);
        }
        Ok(Some(Self(handle)))
    }
}

impl Drop for ProcessInstance {
    fn drop(&mut self) {
        unsafe { CloseHandle(self.0) };
    }
}
