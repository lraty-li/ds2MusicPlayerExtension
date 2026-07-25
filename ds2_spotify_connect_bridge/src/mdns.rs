use socket2::{Domain, Protocol, Socket, Type};
use std::{
    net::{IpAddr, Ipv4Addr, SocketAddr, UdpSocket},
    sync::{Arc, atomic::{AtomicBool, Ordering}},
    thread,
    time::{Duration, Instant},
};

const MDNS_PORT: u16 = 5353;
const MDNS_GROUP: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
const SERVICE: [&str; 3] = ["_spotify-connect", "_tcp", "local"];
const ENUMERATION: [&str; 4] = ["_services", "_dns-sd", "_udp", "local"];
const HOST: [&str; 2] = ["ds2musicplayer", "local"];

pub struct Advertiser(Arc<AtomicBool>);

impl Advertiser {
    pub fn start(device_name: &str, ips: &[IpAddr], port: u16) -> Self {
        let stop = Arc::new(AtomicBool::new(false));
        let name = dns_label(device_name);
        for ip in ips.iter().filter_map(|ip| match ip {
            IpAddr::V4(ip) => Some(*ip),
            IpAddr::V6(_) => None,
        }) {
            let worker_stop = stop.clone();
            let worker_name = name.clone();
            thread::spawn(move || advertise(worker_name, ip, port, worker_stop));
        }
        Self(stop)
    }
}

impl Drop for Advertiser {
    fn drop(&mut self) {
        self.0.store(true, Ordering::Release);
    }
}

fn advertise(device_name: String, ip: Ipv4Addr, port: u16, stop: Arc<AtomicBool>) {
    let socket = match open_socket(ip) {
        Ok(socket) => socket,
        Err(error) => {
            log::warn!("mDNS bind failed on {ip}: {error}");
            return;
        }
    };
    let response = response_packet(&device_name, ip, port);
    let query_names = query_names(&device_name);
    let destination = SocketAddr::from((MDNS_GROUP, MDNS_PORT));
    let mut next_announcement = Instant::now();
    let mut buffer = [0u8; 1500];

    while !stop.load(Ordering::Acquire) {
        if Instant::now() >= next_announcement {
            let _ = socket.send_to(&response, destination);
            next_announcement = Instant::now() + Duration::from_secs(10);
        }
        if let Ok((length, sender)) = socket.recv_from(&mut buffer)
            && is_query(&buffer[..length])
            && query_names.iter().any(|name| contains_name(&buffer[..length], name))
        {
            let target = if wants_unicast_response(&buffer[..length]) || sender.port() != MDNS_PORT {
                sender
            } else {
                destination
            };
            let _ = socket.send_to(&response, target);
        }
    }
}

fn open_socket(interface: Ipv4Addr) -> std::io::Result<UdpSocket> {
    let socket = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    socket.set_reuse_address(true)?;
    socket.set_nonblocking(false)?;
    socket.bind(&SocketAddr::from((Ipv4Addr::UNSPECIFIED, MDNS_PORT)).into())?;
    socket.join_multicast_v4(&MDNS_GROUP, &interface)?;
    socket.set_multicast_if_v4(&interface)?;
    socket.set_multicast_ttl_v4(255)?;
    let socket: UdpSocket = socket.into();
    socket.set_read_timeout(Some(Duration::from_millis(250)))?;
    Ok(socket)
}

fn response_packet(device_name: &str, ip: Ipv4Addr, port: u16) -> Vec<u8> {
    let enumeration = labels(ENUMERATION);
    let service = labels(SERVICE);
    let instance = instance_name(device_name);
    let host = labels(HOST);
    let mut packet = vec![0, 0, 0x84, 0, 0, 0, 0, 5, 0, 0, 0, 0];
    append_record(&mut packet, &enumeration, 12, &name_data(&service));
    append_record(&mut packet, &service, 12, &name_data(&instance));
    append_record(&mut packet, &instance, 33, &srv_data(port));
    append_record(&mut packet, &instance, 16, &txt_data());
    append_record(&mut packet, &host, 1, &ip.octets());
    packet
}

fn append_record(packet: &mut Vec<u8>, name: &[String], record_type: u16, data: &[u8]) {
    append_name(packet, name.iter().map(String::as_str));
    packet.extend_from_slice(&record_type.to_be_bytes());
    packet.extend_from_slice(&1u16.to_be_bytes());
    packet.extend_from_slice(&120u32.to_be_bytes());
    packet.extend_from_slice(&(data.len() as u16).to_be_bytes());
    packet.extend_from_slice(data);
}

fn name_data(name: &[String]) -> Vec<u8> {
    let mut data = Vec::new();
    append_name(&mut data, name.iter().map(String::as_str));
    data
}

fn srv_data(port: u16) -> Vec<u8> {
    let mut data = vec![0, 0, 0, 0];
    data.extend_from_slice(&port.to_be_bytes());
    append_name(&mut data, HOST);
    data
}

fn txt_data() -> Vec<u8> {
    let mut data = Vec::new();
    for value in ["VERSION=1.0", "CPath=/"] {
        data.push(value.len() as u8);
        data.extend_from_slice(value.as_bytes());
    }
    data
}

fn query_names(device_name: &str) -> Vec<Vec<u8>> {
    [
        name_data(&labels(SERVICE)),
        name_data(&labels(ENUMERATION)),
        name_data(&instance_name(device_name)),
        name_data(&labels(HOST)),
    ]
    .to_vec()
}

fn contains_name(packet: &[u8], name: &[u8]) -> bool {
    packet.windows(name.len()).any(|window| window == name)
}

fn is_query(packet: &[u8]) -> bool {
    packet.len() >= 12 && packet[2] & 0x80 == 0
}

fn wants_unicast_response(packet: &[u8]) -> bool {
    if packet.len() < 12 {
        return false;
    }

    let questions = u16::from_be_bytes([packet[4], packet[5]]);
    let mut offset = 12;
    for _ in 0..questions {
        let Some(next) = skip_name(packet, offset) else {
            return false;
        };
        if next + 4 > packet.len() {
            return false;
        }
        let class = u16::from_be_bytes([packet[next + 2], packet[next + 3]]);
        if class & 0x8000 != 0 {
            return true;
        }
        offset = next + 4;
    }
    false
}

fn skip_name(packet: &[u8], mut offset: usize) -> Option<usize> {
    while offset < packet.len() {
        let length = packet[offset];
        if length == 0 {
            return Some(offset + 1);
        }
        if length & 0xC0 == 0xC0 {
            return (offset + 2 <= packet.len()).then_some(offset + 2);
        }
        if length & 0xC0 != 0 {
            return None;
        }
        offset = offset.checked_add(usize::from(length) + 1)?;
    }
    None
}

fn instance_name(device_name: &str) -> Vec<String> {
    let mut result = vec![device_name.to_owned()];
    result.extend(labels(SERVICE));
    result
}

fn labels<const N: usize>(parts: [&str; N]) -> Vec<String> {
    parts.into_iter().map(str::to_owned).collect()
}

fn append_name<'a>(packet: &mut Vec<u8>, labels: impl IntoIterator<Item = &'a str>) {
    for label in labels {
        packet.push(label.len() as u8);
        packet.extend_from_slice(label.as_bytes());
    }
    packet.push(0);
}

fn dns_label(name: &str) -> String {
    if name.is_ascii() && !name.is_empty() && name.len() <= 63 {
        name.to_owned()
    } else {
        String::from("Death Stranding 2")
    }
}
