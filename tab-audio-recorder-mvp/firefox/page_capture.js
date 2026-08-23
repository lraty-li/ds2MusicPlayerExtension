(function installFirefoxPageCapture() {
  const version = 1;
  if (window.__ds2FirefoxPageCapture &&
    window.__ds2FirefoxPageCapture.version === version) return;

  let audioContext = null;
  let sourceNode = null;
  let workletNode = null;
  let processorNode = null;
  let mediaStream = null;
  let workletReadyContext = null;
  let chunk = new Float32Array(960);
  let chunkOffset = 0;
  let active = false;

  window.__ds2FirefoxPageCapture = { version, start, stop, status };

  async function start() {
    stop(false);
    const media = selectCaptureSource();
    if (!media) return { ok: false, error: "adapter did not return media source" };
    try {
      audioContext = ensureAudioContext();
      sourceNode = createMediaSource(audioContext, media);
      safeDisconnect(sourceNode);
      if (await startWorkletGraph()) {
        sourceNode.connect(workletNode);
        workletNode.connect(audioContext.destination);
      } else {
        startScriptProcessorGraph();
      }
      active = true;
      await audioContext.resume();
      postStatus("started", "");
      return { ok: true };
    } catch (error) {
      postStatus("error", String(error));
      stop(false);
      return { ok: false, error: String(error) };
    }
  }

  function stop(notify = true) {
    active = false;
    if (workletNode) {
      workletNode.port.onmessage = null;
      safeDisconnect(workletNode);
      workletNode = null;
    }
    if (processorNode) {
      processorNode.onaudioprocess = null;
      safeDisconnect(processorNode);
      processorNode = null;
    }
    if (sourceNode) {
      safeDisconnect(sourceNode);
      if (audioContext && audioContext.state !== "closed") {
        try {
          sourceNode.connect(audioContext.destination);
        } catch (_) {
        }
      }
    }
    if (mediaStream) {
      for (const track of mediaStream.getTracks()) track.stop();
      mediaStream = null;
    }
    chunk = new Float32Array(960);
    chunkOffset = 0;
    if (notify) postStatus("stopped", "");
    return { ok: true };
  }

  function status() {
    return { ok: true, active };
  }

  function selectCaptureSource() {
    const control = window.__ds2PageMediaControl;
    if (!control || typeof control.captureSource !== "function") return null;
    const media = control.captureSource();
    if (!media || typeof media.tagName !== "string") return null;
    const tag = media.tagName.toLowerCase();
    return tag === "audio" || tag === "video" ? media : null;
  }

  function ensureAudioContext() {
    if (audioContext && audioContext.state !== "closed") return audioContext;
    const Ctor = window.AudioContext || window.webkitAudioContext;
    if (!Ctor) throw new Error("AudioContext unavailable");
    try {
      return new Ctor({ sampleRate: 48000 });
    } catch (_) {
      return new Ctor();
    }
  }

  function createMediaSource(context, media) {
    try {
      const cached = media.__ds2FirefoxMediaSource;
      if (cached && cached.context === context) return cached.node;
      const node = context.createMediaElementSource(media);
      Object.defineProperty(media, "__ds2FirefoxMediaSource", {
        value: { context, node },
        configurable: true
      });
      return node;
    } catch (error) {
      const stream = media.captureStream ? media.captureStream() :
        media.mozCaptureStream ? media.mozCaptureStream() : null;
      if (!stream) throw error;
      mediaStream = stream;
      return context.createMediaStreamSource(stream);
    }
  }

  async function startWorkletGraph() {
    if (!audioContext.audioWorklet || typeof AudioWorkletNode !== "function") return false;
    try {
      await ensureWorkletModule();
      workletNode = new AudioWorkletNode(audioContext, "ds2-firefox-pcm-worklet", {
        numberOfInputs: 1,
        numberOfOutputs: 1,
        outputChannelCount: [2]
      });
      workletNode.port.onmessage = (event) => postAudioChunk(event.data.audio, event.data.frames);
      return true;
    } catch (_) {
      workletNode = null;
      return false;
    }
  }

  function startScriptProcessorGraph() {
    processorNode = audioContext.createScriptProcessor(1024, 2, 2);
    processorNode.onaudioprocess = writeProcessAudio;
    sourceNode.connect(processorNode);
    processorNode.connect(audioContext.destination);
  }

  async function ensureWorkletModule() {
    if (workletReadyContext === audioContext) return;
    const url = URL.createObjectURL(new Blob([readWorkletSource()], {
      type: "text/javascript"
    }));
    try {
      await audioContext.audioWorklet.addModule(url);
      workletReadyContext = audioContext;
    } finally {
      URL.revokeObjectURL(url);
    }
  }

  function writeProcessAudio(event) {
    const input = event.inputBuffer;
    const output = event.outputBuffer;
    for (let ch = 0; ch < output.numberOfChannels; ch++) {
      output.getChannelData(ch).fill(0);
    }
    const left = input.numberOfChannels > 0 ? input.getChannelData(0) : null;
    if (!left) return;
    const right = input.numberOfChannels > 1 ? input.getChannelData(1) : left;
    for (let i = 0; i < left.length; i++) writeFrame(left[i], right[i]);
  }

  function writeFrame(left, right) {
    const at = chunkOffset * 2;
    chunk[at] = left || 0;
    chunk[at + 1] = right || 0;
    chunkOffset++;
    if (chunkOffset < 480) return;
    const audio = chunk.buffer;
    postAudioChunk(audio, 480);
    chunk = new Float32Array(960);
    chunkOffset = 0;
  }

  function postAudioChunk(audio, frames) {
    window.postMessage({
      type: "ds2-firefox-audio-chunk",
      audio,
      frames,
      sampleRate: audioContext ? audioContext.sampleRate : 48000
    }, "*", [audio]);
  }

  function safeDisconnect(node) {
    try {
      node.disconnect();
    } catch (_) {
    }
  }

  function postStatus(stage, error) {
    window.postMessage({
      type: "ds2-firefox-capture-status",
      stage,
      error
    }, "*");
  }

  function readWorkletSource() {
    return `
class Ds2FirefoxPcmWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.chunkFrames = 480;
    this.buffer = new Float32Array(this.chunkFrames * 2);
    this.offset = 0;
  }
  process(inputs, outputs) {
    const output = outputs[0];
    if (output) {
      for (let ch = 0; ch < output.length; ch++) output[ch].fill(0);
    }
    const input = inputs[0];
    if (!input || input.length === 0 || input[0].length === 0) {
      this.writeSilence(128);
      return true;
    }
    const left = input[0];
    const right = input.length > 1 ? input[1] : left;
    for (let i = 0; i < left.length; i++) this.writeFrame(left[i], right[i]);
    return true;
  }
  writeSilence(frames) {
    for (let i = 0; i < frames; i++) this.writeFrame(0, 0);
  }
  writeFrame(left, right) {
    const index = this.offset * 2;
    this.buffer[index] = left || 0;
    this.buffer[index + 1] = right || 0;
    this.offset++;
    if (this.offset >= this.chunkFrames) {
      const audio = this.buffer.buffer;
      this.port.postMessage({ audio, frames: this.chunkFrames }, [audio]);
      this.buffer = new Float32Array(this.chunkFrames * 2);
      this.offset = 0;
    }
  }
}
registerProcessor("ds2-firefox-pcm-worklet", Ds2FirefoxPcmWorklet);
`;
  }
})();
