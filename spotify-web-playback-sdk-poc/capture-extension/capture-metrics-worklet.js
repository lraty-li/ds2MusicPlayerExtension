class CaptureMetricsProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.targetFrames = Math.max(128, Math.round(sampleRate));
    this.resetWindow();
  }

  resetWindow() {
    this.frames = 0;
    this.sampleCount = 0;
    this.nonzeroCount = 0;
    this.nonFiniteSamples = 0;
    this.sumSquares = 0;
    this.peak = 0;
    this.channels = 0;
  }

  process(inputs) {
    const input = inputs[0] || [];
    const frames = input.length > 0 ? input[0].length : 128;
    this.frames += frames;
    this.channels = Math.max(this.channels, input.length);

    for (const channel of input) {
      for (let index = 0; index < channel.length; index++) {
        const rawValue = channel[index];
        if (!Number.isFinite(rawValue)) this.nonFiniteSamples++;
        const value = Number.isFinite(rawValue) ? rawValue : 0;
        const absolute = Math.abs(value);
        this.sumSquares += value * value;
        this.sampleCount++;
        if (value !== 0) this.nonzeroCount++;
        if (absolute > this.peak) this.peak = absolute;
      }
    }

    if (this.frames >= this.targetFrames) {
      this.port.postMessage({
        channels: this.channels,
        rms: this.sampleCount > 0
          ? Math.sqrt(this.sumSquares / this.sampleCount)
          : 0,
        peak: this.peak,
        nonzeroRatio: this.sampleCount > 0
          ? this.nonzeroCount / this.sampleCount
          : 0,
        frames: this.frames,
        elapsedMs: this.frames * 1000 / sampleRate,
        nonFiniteSamples: this.nonFiniteSamples
      });
      this.resetWindow();
    }

    return true;
  }
}

registerProcessor("capture-metrics-processor", CaptureMetricsProcessor);
