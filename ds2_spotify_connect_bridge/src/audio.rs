use librespot::playback::{
    audio_backend::{Sink, SinkError, SinkResult},
    convert::Converter,
    decoder::AudioPacket,
};
use std::sync::mpsc::SyncSender;

const SOURCE_RATE: f64 = 44_100.0;
const OUTPUT_RATE: f64 = 48_000.0;
const CHANNELS: usize = 2;
const FRAMES_PER_PACKET: usize = 480;

pub struct Ds2AudioSink {
    sender: SyncSender<Vec<u8>>,
    resampler: LinearStereoResampler,
    packetizer: Packetizer,
}

impl Ds2AudioSink {
    pub fn new(sender: SyncSender<Vec<u8>>) -> Self {
        Self {
            sender,
            resampler: LinearStereoResampler::new(),
            packetizer: Packetizer::new(),
        }
    }
}

impl Sink for Ds2AudioSink {
    fn write(&mut self, packet: AudioPacket, _converter: &mut Converter) -> SinkResult<()> {
        let samples = match packet {
            AudioPacket::Samples(samples) => samples,
            AudioPacket::Raw(_) => {
                return Err(SinkError::InvalidParams(String::from("raw audio is unsupported")));
            }
        };

        let output = self.resampler.process(&samples);
        for encoded_packet in self.packetizer.push(&output) {
            let _ = self.sender.try_send(encoded_packet);
        }
        Ok(())
    }
}

struct LinearStereoResampler {
    pending: Vec<[f64; CHANNELS]>,
    position: f64,
}

impl LinearStereoResampler {
    fn new() -> Self {
        Self { pending: Vec::new(), position: 0.0 }
    }

    fn process(&mut self, input: &[f64]) -> Vec<f32> {
        for frame in input.chunks_exact(CHANNELS) {
            self.pending.push([frame[0], frame[1]]);
        }

        let step = SOURCE_RATE / OUTPUT_RATE;
        let mut output = Vec::new();
        while self.position + 1.0 < self.pending.len() as f64 {
            let index = self.position as usize;
            let fraction = self.position - index as f64;
            let first = self.pending[index];
            let second = self.pending[index + 1];
            output.push((first[0] + (second[0] - first[0]) * fraction) as f32);
            output.push((first[1] + (second[1] - first[1]) * fraction) as f32);
            self.position += step;
        }

        let consumed = self.position as usize;
        if consumed > 0 {
            self.pending.drain(0..consumed);
            self.position -= consumed as f64;
        }
        output
    }
}

struct Packetizer {
    pending: Vec<f32>,
    sequence: u64,
}

impl Packetizer {
    fn new() -> Self {
        Self { pending: Vec::new(), sequence: 0 }
    }

    fn push(&mut self, samples: &[f32]) -> Vec<Vec<u8>> {
        self.pending.extend_from_slice(samples);
        let samples_per_packet = FRAMES_PER_PACKET * CHANNELS;
        let mut result = Vec::new();
        while self.pending.len() >= samples_per_packet {
            let samples: Vec<f32> = self.pending.drain(0..samples_per_packet).collect();
            result.push(self.encode(&samples));
        }
        result
    }

    fn encode(&mut self, samples: &[f32]) -> Vec<u8> {
        let payload_bytes = (samples.len() * std::mem::size_of::<f32>()) as u32;
        let mut packet = Vec::with_capacity(32 + payload_bytes as usize);
        packet.extend_from_slice(&0x4453_3241u32.to_le_bytes());
        packet.extend_from_slice(&2u16.to_le_bytes());
        packet.extend_from_slice(&(CHANNELS as u16).to_le_bytes());
        packet.extend_from_slice(&(OUTPUT_RATE as u32).to_le_bytes());
        packet.extend_from_slice(&(FRAMES_PER_PACKET as u32).to_le_bytes());
        packet.extend_from_slice(&self.sequence.to_le_bytes());
        packet.extend_from_slice(&payload_bytes.to_le_bytes());
        packet.extend_from_slice(&2u16.to_le_bytes());
        packet.extend_from_slice(&32u16.to_le_bytes());
        for sample in samples {
            packet.extend_from_slice(&sample.to_le_bytes());
        }
        self.sequence += 1;
        packet
    }
}
