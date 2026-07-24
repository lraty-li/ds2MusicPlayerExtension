use librespot::connect::Spirc;
use std::sync::{Arc, Mutex};

#[derive(Clone)]
pub struct TrackSnapshot {
    pub generation: u64,
    pub metadata_json: String,
    pub jacket_json: Option<String>,
}

pub struct SharedBridgeState {
    snapshot: Mutex<Option<TrackSnapshot>>,
    spirc: Mutex<Option<Spirc>>,
}

impl SharedBridgeState {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { snapshot: Mutex::new(None), spirc: Mutex::new(None) })
    }

    pub fn set_spirc(&self, spirc: Spirc) {
        if let Ok(mut current) = self.spirc.lock() {
            *current = Some(spirc);
        }
    }

    pub fn clear_spirc(&self) {
        if let Ok(mut current) = self.spirc.lock() {
            *current = None;
        }
    }

    pub fn pause(&self) {
        if let Ok(current) = self.spirc.lock() {
            if let Some(spirc) = current.as_ref() {
                let _ = spirc.pause();
            }
        }
    }

    pub fn resume(&self) {
        if let Ok(current) = self.spirc.lock() {
            if let Some(spirc) = current.as_ref() {
                let _ = spirc.play();
            }
        }
    }

    pub fn replace_snapshot(&self, snapshot: TrackSnapshot) {
        if let Ok(mut current) = self.snapshot.lock() {
            *current = Some(snapshot);
        }
    }

    pub fn clear_snapshot(&self) {
        if let Ok(mut current) = self.snapshot.lock() {
            *current = None;
        }
    }

    pub fn set_jacket(&self, generation: u64, jacket_json: String) {
        if let Ok(mut current) = self.snapshot.lock() {
            if let Some(snapshot) = current.as_mut()
                && snapshot.generation == generation
            {
                snapshot.jacket_json = Some(jacket_json);
            }
        }
    }

    pub fn snapshot(&self) -> Option<TrackSnapshot> {
        self.snapshot.lock().ok().and_then(|current| current.clone())
    }
}
