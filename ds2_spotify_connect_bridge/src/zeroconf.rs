use aes::cipher::{KeyIvInit, StreamCipher};
use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};
use bytes::Bytes;
use ctr::Ctr128BE;
use hmac::{Hmac, Mac};
use http_body_util::{BodyExt, Full};
use hyper::{
    Method, Request, Response, StatusCode,
    body::Incoming,
    header::{CONTENT_TYPE, HeaderValue},
    server::conn::http1,
    service::service_fn,
};
use hyper_util::rt::TokioIo;
use librespot::{
    core::{authentication::Credentials, config::DeviceType, diffie_hellman::DhLocalKeys},
};
use sha1::{Digest, Sha1};
use std::{
    collections::BTreeMap,
    convert::Infallible,
    io,
    net::{Ipv4Addr, SocketAddr, TcpListener},
    sync::{Arc, Mutex},
};
use tokio::sync::{mpsc, oneshot};

type Aes128Ctr = Ctr128BE<aes::Aes128>;
type Params = BTreeMap<String, String>;
const RESPONSE_SOURCE: &str = "DS2MusicPlayer";

pub struct Config {
    name: String,
    device_id: String,
    client_id: String,
    device_type: DeviceType,
    is_group: bool,
}

impl Config {
    pub fn new(
        name: String,
        device_id: String,
        client_id: String,
        device_type: DeviceType,
        is_group: bool,
    ) -> Self {
        Self { name, device_id, client_id, device_type, is_group }
    }
}

pub struct Server {
    close_tx: oneshot::Sender<()>,
    task: tokio::task::JoinHandle<()>,
}

struct Handler {
    config: Config,
    active_user: Mutex<Option<String>>,
    keys: DhLocalKeys,
    credentials_tx: mpsc::UnboundedSender<Credentials>,
}

impl Handler {
    fn get_info(&self) -> Response<Full<Bytes>> {
        crate::write_status("zeroconf getInfo request");
        let device_type: &str = self.config.device_type.into();
        let active_user = self.active_user.lock().ok().and_then(|user| user.clone());
        let group_status = if self.config.is_group { "GROUP" } else { "NONE" };
        json_response(
            StatusCode::OK,
            serde_json::json!({
                "status": 101,
                "statusString": "OK",
                "spotifyError": 0,
                "responseSource": RESPONSE_SOURCE,
                "version": "2.10.0",
                "deviceID": self.config.device_id,
                "deviceType": device_type,
                "remoteName": self.config.name,
                "publicKey": BASE64.encode(self.keys.public_key()),
                "brandDisplayName": "DS2MusicPlayer",
                "modelDisplayName": "Death Stranding 2",
                "libraryVersion": "DS2MusicPlayer",
                "resolverVersion": "1",
                "groupStatus": group_status,
                "tokenType": "default",
                "clientID": self.config.client_id,
                "productID": 0,
                "scope": "streaming",
                "availability": "",
                "supported_drm_media_formats": [],
                "supported_capabilities": 1,
                "accountReq": "PREMIUM",
                "activeUser": active_user.unwrap_or_default(),
                "aliases": []
            }),
        )
    }

    fn add_user(&self, params: &Params) -> Response<Full<Bytes>> {
        crate::write_status("zeroconf addUser request");
        let Some(username) = params.get("userName") else {
            return failure(StatusCode::BAD_REQUEST, 303, "ERROR-INVALID-ARGUMENTS");
        };
        let Some(blob) = params.get("blob") else {
            return failure(StatusCode::BAD_REQUEST, 303, "ERROR-INVALID-ARGUMENTS");
        };
        let Some(client_key) = params.get("clientKey") else {
            return failure(StatusCode::BAD_REQUEST, 303, "ERROR-INVALID-ARGUMENTS");
        };
        if !params.contains_key("tokenType") {
            return failure(StatusCode::BAD_REQUEST, 303, "ERROR-INVALID-ARGUMENTS");
        }
        let result = decrypt_credentials(username, blob, client_key, &self.config.device_id, &self.keys);
        let Ok(credentials) = result else {
            return failure(StatusCode::BAD_REQUEST, 303, "ERROR-INVALID-ARGUMENTS");
        };
        if self.credentials_tx.send(credentials).is_err() {
            return failure(StatusCode::INTERNAL_SERVER_ERROR, 103, "ERROR-UNKNOWN");
        }
        if let Ok(mut active_user) = self.active_user.lock() {
            *active_user = Some(username.clone());
        }
        success()
    }

    async fn handle(self: Arc<Self>, request: Request<Incoming>) -> Response<Full<Bytes>> {
        let (parts, body) = request.into_parts();
        let Ok(body) = body.collect().await else {
            return failure(StatusCode::BAD_REQUEST, 102, "ERROR-BAD-REQUEST");
        };
        let params = params(parts.uri.query(), &body.to_bytes());
        match (parts.method, params.get("action").map(String::as_str)) {
            (Method::GET, Some("getInfo")) => self.get_info(),
            (Method::POST, Some("addUser")) => self.add_user(&params),
            (_, None) => failure(StatusCode::BAD_REQUEST, 301, "ERROR-MISSING-ACTION"),
            _ => failure(StatusCode::BAD_REQUEST, 302, "ERROR-INVALID-ACTION"),
        }
    }
}

impl Server {
    pub fn start(config: Config, port: u16) -> io::Result<(Self, mpsc::UnboundedReceiver<Credentials>)> {
        let listener = TcpListener::bind(SocketAddr::from((Ipv4Addr::UNSPECIFIED, port)))?;
        listener.set_nonblocking(true)?;
        let listener = tokio::net::TcpListener::from_std(listener)?;
        crate::write_status("zeroconf HTTP listener bound");
        let (credentials_tx, credentials_rx) = mpsc::unbounded_channel();
        let handler = Arc::new(Handler {
            config,
            active_user: Mutex::new(None),
            keys: DhLocalKeys::random(&mut rand::rng()),
            credentials_tx,
        });
        let (close_tx, mut close_rx) = oneshot::channel();
        let task = tokio::spawn(async move {
            let http = http1::Builder::new();
            loop {
                tokio::select! {
                    result = listener.accept() => match result {
                        Ok((stream, _)) => {
                            let handler = handler.clone();
                            let service = service_fn(move |request| {
                                let handler = handler.clone();
                                async move {
                                    Ok::<_, Infallible>(handler.handle(request).await)
                                }
                            });
                            let connection = http.serve_connection(TokioIo::new(stream), service);
                            tokio::spawn(async move { let _ = connection.await; });
                        }
                        Err(error) => log::warn!("ZeroConf accept failed: {error}"),
                    },
                    _ = &mut close_rx => break,
                }
            }
        });
        Ok((Self { close_tx, task }, credentials_rx))
    }

    #[allow(dead_code)]
    pub async fn shutdown(self) {
        let _ = self.close_tx.send(());
        let _ = self.task.await;
    }
}

fn params(query: Option<&str>, body: &[u8]) -> Params {
    let mut result = Params::new();
    if let Some(query) = query {
        result.extend(form_urlencoded::parse(query.as_bytes()).map(|(key, value)| (key.into_owned(), value.into_owned())));
    }
    result.extend(form_urlencoded::parse(body).map(|(key, value)| (key.into_owned(), value.into_owned())));
    result
}

fn decrypt_credentials(
    username: &str,
    blob: &str,
    client_key: &str,
    device_id: &str,
    keys: &DhLocalKeys,
) -> Result<Credentials, ()> {
    let encrypted_blob = BASE64.decode(blob.as_bytes()).map_err(|_| ())?;
    if encrypted_blob.len() < 36 {
        return Err(());
    }
    let client_key = BASE64.decode(client_key.as_bytes()).map_err(|_| ())?;
    let shared_key = keys.shared_secret(&client_key);
    let base_key = Sha1::digest(shared_key);
    let mut checksum = Hmac::<Sha1>::new_from_slice(&base_key[..16]).map_err(|_| ())?;
    checksum.update(b"checksum");
    let checksum_key = checksum.finalize().into_bytes();
    let mut encryption = Hmac::<Sha1>::new_from_slice(&base_key[..16]).map_err(|_| ())?;
    encryption.update(b"encryption");
    let encryption_key = encryption.finalize().into_bytes();
    let split = encrypted_blob.len() - 20;
    let (iv, encrypted, received_checksum) = (&encrypted_blob[..16], &encrypted_blob[16..split], &encrypted_blob[split..]);
    let mut verify = Hmac::<Sha1>::new_from_slice(&checksum_key).map_err(|_| ())?;
    verify.update(encrypted);
    verify.verify_slice(received_checksum).map_err(|_| ())?;
    let mut decrypted = encrypted.to_vec();
    let mut cipher = Aes128Ctr::new_from_slices(&encryption_key[..16], iv).map_err(|_| ())?;
    cipher.apply_keystream(&mut decrypted);
    Credentials::with_blob(username, decrypted, device_id).map_err(|_| ())
}

fn success() -> Response<Full<Bytes>> {
    json_response(StatusCode::OK, serde_json::json!({
        "status": 101, "statusString": "OK", "spotifyError": 0, "responseSource": RESPONSE_SOURCE
    }))
}

fn failure(status: StatusCode, code: i32, message: &str) -> Response<Full<Bytes>> {
    json_response(status, serde_json::json!({
        "status": code, "statusString": message, "spotifyError": 0, "responseSource": RESPONSE_SOURCE
    }))
}

fn json_response(status: StatusCode, value: serde_json::Value) -> Response<Full<Bytes>> {
    Response::builder()
        .status(status)
        .header(CONTENT_TYPE, HeaderValue::from_static("application/json; charset=utf-8"))
        .header("Content-Security-Policy", HeaderValue::from_static("frame-ancestors 'none'"))
        .body(Full::new(Bytes::from(value.to_string())))
        .unwrap_or_else(|_| Response::new(Full::new(Bytes::new())))
}
