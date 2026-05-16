class PcmChunkWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.chunkFrames = 480;
    this.buffer = new Float32Array(this.chunkFrames * 2);
    this.offset = 0;
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || input.length === 0 || input[0].length === 0) {
      this.writeSilence(128);
      return true;
    }

    const left = input[0];
    const right = input.length > 1 ? input[1] : left;
    for (let i = 0; i < left.length; i++) {
      this.writeFrame(left[i], right[i]);
    }
    return true;
  }

  writeSilence(frames) {
    for (let i = 0; i < frames; i++) {
      this.writeFrame(0, 0);
    }
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

registerProcessor("pcm-chunk-worklet", PcmChunkWorklet);
