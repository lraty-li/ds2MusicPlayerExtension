(() => {
  "use strict";
  const probe = window.__ds2AudioFrameProbe;
  if (!probe || probe.graphExtensionInstalled) return;
  probe.graphExtensionInstalled = true;

  function sampleGraph(record) {
    const { analyser, samples } = record.graph;
    analyser.getFloatTimeDomainData(samples);
    let sumSquares = 0;
    let peak = 0;
    let nonzero = 0;
    for (const sample of samples) {
      const absolute = Math.abs(sample);
      sumSquares += sample * sample;
      peak = Math.max(peak, absolute);
      if (absolute > 0.000001) nonzero++;
    }
    record.directRms = Math.sqrt(sumSquares / samples.length);
    record.directPeak = peak;
    record.directNonzero = nonzero / samples.length;
    if (peak > 0.0001 && record.directNonzero > 0.001) {
      record.directWindows++;
    }
    probe.report();
  }

  async function attachMedia(record) {
    if (record.graphState !== "none") return;
    record.graphState = "attaching";
    record.graphError = "";
    probe.report();
    try {
      const context = new AudioContext({ sampleRate: 48000 });
      const source = context.createMediaElementSource(record.element);
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      analyser.smoothingTimeConstant = 0;
      const pcmTap = await probe.createPcmTap(context, record);
      source.connect(pcmTap);
      pcmTap.connect(analyser);
      analyser.connect(context.destination);
      record.graph = {
        context,
        source,
        pcmTap,
        analyser,
        samples: new Float32Array(analyser.fftSize),
        timer: 0
      };
      record.graphState = "attached";
      await context.resume();
      record.graph.timer = window.setInterval(
        () => sampleGraph(record),
        250
      );
    } catch (error) {
      record.graphState = "error";
      record.graphError = `${error.name || "Error"}: ${error.message}`;
    }
    probe.report();
  }

  probe.commandHandlers.set("attach-media", async () => {
    await Promise.all(
      [...probe.mediaElements.values()].map(attachMedia)
    );
    probe.report();
  });
})();
