use crate::state::{SharedBridgeState, TrackSnapshot};
use base64::{Engine as _, engine::general_purpose::STANDARD};
use librespot::{
    metadata::audio::{AudioItem, UniqueFields},
    playback::player::{PlayerEvent, PlayerEventChannel},
};
use serde_json::json;
use std::{io::Read, sync::Arc, thread};

const MAX_JACKET_BYTES: u64 = 2 * 1024 * 1024;

pub fn start(mut events: PlayerEventChannel, state: Arc<SharedBridgeState>) {
    thread::spawn(move || {
        let mut generation = 0u64;
        while let Some(event) = events.blocking_recv() {
            if let PlayerEvent::TrackChanged { audio_item } = event {
                generation += 1;
                publish_track(*audio_item, generation, &state);
            }
        }
    });
}

fn publish_track(item: AudioItem, generation: u64, state: &SharedBridgeState) {
    let track_key = format!("{}:{generation}", item.uri);
    let metadata_json = json!({
        "type": "metadata",
        "title": item.name,
        "artist": artist_name(&item),
        "adapter": "spotify_connect",
        "host": "spotify",
        "trackKey": track_key,
    })
    .to_string();
    state.replace_snapshot(TrackSnapshot { generation, metadata_json, jacket_json: None });

    if let Some(jacket_json) = download_jacket(&item, &track_key) {
        state.set_jacket(generation, jacket_json);
    }
}

fn artist_name(item: &AudioItem) -> String {
    match &item.unique_fields {
        UniqueFields::Track { artists, .. } => artists
            .0
            .iter()
            .map(|artist| artist.name.as_str())
            .collect::<Vec<_>>()
            .join(", "),
        UniqueFields::Episode { show_name, .. } => show_name.clone(),
        UniqueFields::Local { artists, .. } => artists.clone().unwrap_or_default(),
    }
}

fn download_jacket(item: &AudioItem, track_key: &str) -> Option<String> {
    for cover in &item.covers {
        let response = match ureq::get(&cover.url)
            .set("User-Agent", "DS2MusicPlayer/SpotifyConnect")
            .call()
        {
            Ok(response) => response,
            Err(_) => continue,
        };
        let mime = response
            .header("content-type")
            .unwrap_or("image/jpeg")
            .split(';')
            .next()
            .unwrap_or("image/jpeg")
            .to_owned();
        let mut bytes = Vec::new();
        let read = response
            .into_reader()
            .take(MAX_JACKET_BYTES + 1)
            .read_to_end(&mut bytes);
        if read.is_err() || bytes.is_empty() || bytes.len() as u64 > MAX_JACKET_BYTES {
            continue;
        }
        return Some(
            json!({
                "type": "jacket",
                "mime": mime,
                "source": cover.url,
                "jacketSource": "spotify_connect",
                "bytes": bytes.len(),
                "data": STANDARD.encode(bytes),
                "trackKey": track_key,
            })
            .to_string(),
        );
    }
    None
}
