(() => {
  "use strict";
  const probe = window.__ds2AudioFrameProbe;
  if (!probe || probe.pcmBridgeExtensionInstalled) return;
  probe.pcmBridgeExtensionInstalled = true;

  const CHANNELS = 2;
  const CHUNK_FRAMES = 4800;
  const WORKLET_NAME = "ds2-pcm-tap-v1";

  function errorText(error) {
    return `${error?.name || "Error"}: ${error?.message || String(error)}`;
  }

  function encodeBase64(buffer) {
    const bytes = new Uint8Array(buffer);
    let binary = "";
    for (let offset = 0; offset < bytes.length; offset += 16384) {
      binary += String.fromCharCode(
        ...bytes.subarray(offset, offset + 16384)
      );
    }
    return btoa(binary);
  }

  function emitChunk(record, buffer, frames, rate, channels) {
    const bridge = record.bridge;
    try {
      const sequence = bridge.nextSequence++;
      window.top.postMessage({
        channel: probe.channel,
        type: "pcm-chunk",
        streamId: bridge.streamId,
        sequence,
        sampleRate: rate,
        channels,
        frames,
        payload: encodeBase64(buffer)
      }, "*");
      bridge.sentChunks++;
      bridge.sentFrames += frames;
      bridge.state = "streaming";
      bridge.error = "";
      if (bridge.sentChunks === 1 || bridge.sentChunks % 10 === 0) {
        probe.report();
      }
    } catch (error) {
      bridge.state = "error";
      bridge.error = errorText(error);
      probe.report();
    }
  }

  function workletSource() {
    return `
class Ds2PcmTap extends AudioWorkletProcessor {
  constructor() {
    super();
    this.channels = ${CHANNELS};
    this.chunkFrames = ${CHUNK_FRAMES};
    this.samples = new Int16Array(this.chunkFrames * this.channels);
    this.writeFrame = 0;
  }

  toInt16(value) {
    const clipped = Math.max(-1, Math.min(1, value));
    return clipped < 0
      ? Math.round(clipped * 32768)
      : Math.round(clipped * 32767);
  }

  flush() {
    const buffer = this.samples.buffer;
    this.port.postMessage({
      frames: this.chunkFrames,
      channels: this.channels,
      sampleRate,
      samples: buffer
    }, [buffer]);
    this.samples = new Int16Array(this.chunkFrames * this.channels);
    this.writeFrame = 0;
  }

  process(inputs, outputs) {
    const input = inputs[0] || [];
    const output = outputs[0] || [];
    if (input.length === 0) return true;
    const frames = input[0].length;
    for (let channel = 0; channel < output.length; channel++) {
      const source = input[Math.min(channel, input.length - 1)];
      output[channel].set(source);
    }
    for (let frame = 0; frame < frames; frame++) {
      for (let channel = 0; channel < this.channels; channel++) {
        const source = input[Math.min(channel, input.length - 1)];
        this.samples[
          this.writeFrame * this.channels + channel
        ] = this.toInt16(source[frame]);
      }
      this.writeFrame++;
      if (this.writeFrame === this.chunkFrames) this.flush();
    }
    return true;
  }
}
registerProcessor("${WORKLET_NAME}", Ds2PcmTap);
`;
  }

  async function createWorkletTap(context, record) {
    if (!context.audioWorklet || typeof AudioWorkletNode !== "function") {
      throw new Error("AudioWorklet unavailable");
    }
    const moduleUrl = URL.createObjectURL(
      new Blob([workletSource()], { type: "text/javascript" })
    );
    try {
      await context.audioWorklet.addModule(moduleUrl);
    } finally {
      URL.revokeObjectURL(moduleUrl);
    }
    const node = new AudioWorkletNode(context, WORKLET_NAME, {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [CHANNELS],
      channelCount: CHANNELS,
      channelCountMode: "explicit",
      channelInterpretation: "speakers"
    });
    node.port.onmessage = ({ data }) => {
      if (data?.samples instanceof ArrayBuffer) {
        emitChunk(
          record,
          data.samples,
          Number(data.frames),
          Number(data.sampleRate),
          Number(data.channels)
        );
      }
    };
    node.addEventListener("processorerror", () => {
      record.bridge.state = "error";
      record.bridge.error = "AudioWorklet processorerror";
      probe.report();
    });
    return node;
  }

  function floatBufferToPcm16(input) {
    const frames = input.length;
    const samples = new Int16Array(frames * CHANNELS);
    for (let frame = 0; frame < frames; frame++) {
      for (let channel = 0; channel < CHANNELS; channel++) {
        const source = input.getChannelData(
          Math.min(channel, input.numberOfChannels - 1)
        );
        const clipped = Math.max(-1, Math.min(1, source[frame]));
        samples[frame * CHANNELS + channel] = clipped < 0
          ? Math.round(clipped * 32768)
          : Math.round(clipped * 32767);
      }
    }
    return samples;
  }

  function createScriptProcessorTap(context, record) {
    if (typeof context.createScriptProcessor !== "function") {
      throw new Error("ScriptProcessor unavailable");
    }
    const node = context.createScriptProcessor(4096, CHANNELS, CHANNELS);
    node.onaudioprocess = ({ inputBuffer, outputBuffer }) => {
      for (let channel = 0;
        channel < outputBuffer.numberOfChannels;
        channel++) {
        const source = inputBuffer.getChannelData(
          Math.min(channel, inputBuffer.numberOfChannels - 1)
        );
        outputBuffer.getChannelData(channel).set(source);
      }
      const samples = floatBufferToPcm16(inputBuffer);
      emitChunk(
        record,
        samples.buffer,
        inputBuffer.length,
        inputBuffer.sampleRate,
        CHANNELS
      );
    };
    return node;
  }

  probe.createPcmTap = async (context, record) => {
    record.bridge = {
      state: "starting",
      method: "",
      note: "",
      error: "",
      streamId: `${probe.frameId}-${record.id}-${Date.now()}`,
      nextSequence: 0,
      sentChunks: 0,
      sentFrames: 0
    };
    probe.report();
    try {
      const node = await createWorkletTap(context, record);
      record.bridge.state = "ready";
      record.bridge.method = "AudioWorklet";
      probe.report();
      return node;
    } catch (workletError) {
      record.bridge.note = errorText(workletError);
    }
    try {
      const node = createScriptProcessorTap(context, record);
      record.bridge.state = "ready";
      record.bridge.method = "ScriptProcessor fallback";
      probe.report();
      return node;
    } catch (fallbackError) {
      record.bridge.state = "error";
      record.bridge.method = "passthrough only";
      record.bridge.error =
        `${record.bridge.note}; ${errorText(fallbackError)}`;
      probe.report();
      return context.createGain();
    }
  };
})();
