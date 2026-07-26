(() => {
  "use strict";
  const probe = window.__ds2AudioFrameProbe;
  if (!probe || probe.sharedRingExtensionInstalled) return;
  probe.sharedRingExtensionInstalled = true;
  probe.sharedPcmRing = null;
  probe.sharedRingError = "";

  function validLayout(layout, buffer) {
    const integer = (value, minimum) =>
      Number.isSafeInteger(value) && value >= minimum;
    return (
      layout?.type === "ds2-pcm-ring-v1" &&
      layout.version === 1 &&
      integer(layout.bufferBytes, 1) &&
      integer(layout.headerBytes, 1) &&
      integer(layout.slotCount, 2) &&
      integer(layout.slotBytes, 1) &&
      integer(layout.slotHeaderBytes, 36) &&
      integer(layout.payloadCapacity, 1) &&
      integer(layout.slotMagic, 1) &&
      integer(layout.commitXor, 1) &&
      buffer instanceof ArrayBuffer &&
      buffer.byteLength === layout.bufferBytes &&
      layout.headerBytes +
        layout.slotCount * layout.slotBytes <= buffer.byteLength &&
      layout.slotHeaderBytes + layout.payloadCapacity <= layout.slotBytes
    );
  }

  function releaseRing() {
    const ring = probe.sharedPcmRing;
    if (!ring) return;
    try {
      window.chrome?.webview?.releaseBuffer(ring.buffer);
    } catch {
    }
    probe.sharedPcmRing = null;
  }

  function acceptRing(event) {
    const layout = event.additionalData;
    if (layout?.type !== "ds2-pcm-ring-v1") return;
    try {
      const buffer = event.getBuffer();
      if (!validLayout(layout, buffer)) {
        throw new Error("invalid shared ring layout");
      }
      releaseRing();
      probe.sharedPcmRing = {
        buffer,
        layout,
        view: new DataView(buffer)
      };
      probe.sharedRingError = "";
    } catch (error) {
      probe.sharedRingError =
        `${error.name || "Error"}: ${error.message}`;
    }
    probe.report();
  }

  function checksum(bytes) {
    let value = 2166136261;
    for (const byte of bytes) {
      value = Math.imul(value ^ byte, 16777619) >>> 0;
    }
    return value;
  }

  probe.writeSharedPcmChunk = (
    record,
    sampleBuffer,
    frames,
    sampleRate,
    channels,
    sequence
  ) => {
    const ring = probe.sharedPcmRing;
    if (!ring) return "unavailable";
    try {
      const source = new Uint8Array(sampleBuffer);
      const { layout, view, buffer } = ring;
      if (source.byteLength > layout.payloadCapacity) {
        throw new Error("PCM payload exceeds shared ring slot");
      }
      const slot = sequence % layout.slotCount;
      const offset = layout.headerBytes + slot * layout.slotBytes;
      const commitOffset = offset + 32;
      if (view.getUint32(commitOffset, true) !== 0) {
        return "dropped";
      }

      const sequence32 = sequence >>> 0;
      view.setUint32(commitOffset, 0, true);
      view.setUint32(offset, layout.slotMagic, true);
      view.setUint32(offset + 4, layout.version, true);
      view.setUint32(offset + 8, sequence32, true);
      view.setUint32(offset + 12, frames, true);
      view.setUint32(offset + 16, sampleRate, true);
      view.setUint32(offset + 20, channels, true);
      view.setUint32(offset + 24, source.byteLength, true);
      new Uint8Array(
        buffer,
        offset + layout.slotHeaderBytes,
        source.byteLength
      ).set(source);
      view.setUint32(offset + 28, checksum(source), true);
      view.setUint32(
        commitOffset,
        (sequence32 ^ layout.commitXor) >>> 0,
        true
      );
      window.top.postMessage({
        channel: probe.channel,
        type: "pcm-ring-commit",
        streamId: record.bridge.streamId,
        sequence,
        slot
      }, "*");
      return "sent";
    } catch (error) {
      probe.sharedRingError =
        `${error.name || "Error"}: ${error.message}`;
      probe.report();
      return "dropped";
    }
  };

  window.chrome?.webview?.addEventListener(
    "sharedbufferreceived", acceptRing
  );
  window.addEventListener("pagehide", releaseRing, { once: true });
})();
