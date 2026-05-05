class PcmChunkWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.chunkFrames = 960;
    this.buffer = new Int16Array(this.chunkFrames * 2);
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
    this.buffer[index] = this.floatToPcm16(left);
    this.buffer[index + 1] = this.floatToPcm16(right);
    this.offset++;

    if (this.offset >= this.chunkFrames) {
      const pcm = this.buffer.buffer;
      this.port.postMessage({ pcm, frames: this.chunkFrames }, [pcm]);
      this.buffer = new Int16Array(this.chunkFrames * 2);
      this.offset = 0;
    }
  }

  floatToPcm16(value) {
    const clamped = Math.max(-1, Math.min(1, value || 0));
    return clamped < 0 ? clamped * 32768 : clamped * 32767;
  }
}

registerProcessor("pcm-chunk-worklet", PcmChunkWorklet);
