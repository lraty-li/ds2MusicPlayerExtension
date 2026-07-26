export function buildProbeState(frameReports, requestedMode) {
  const frames = [...frameReports.values()];
  const contexts = frames.flatMap((frame) =>
    frame.contexts.map((context) => ({
      ...context,
      frameId: frame.frameId,
      origin: frame.origin
    }))
  );
  const media = frames.flatMap((frame) => frame.media);
  const noneContexts = contexts.filter(({ sink }) => sink === "none").length;
  const playingMedia = media.filter(({ playing }) => playing).length;
  const uncoveredPlaying = media.filter(
    ({ playing, graphState }) => playing && graphState !== "attached"
  ).length;
  const attachedMedia = media.filter(
    ({ graphState }) => graphState === "attached"
  ).length;
  const graphErrors = media.filter(
    ({ graphState }) => graphState === "error"
  ).length;
  const contextErrors = contexts.filter(({ error }) => !!error).length;
  const directRms = maximum(media.map(({ directRms }) => directRms));
  const directPeak = maximum(media.map(({ directPeak }) => directPeak));
  const directNonzero = maximum(
    media.map(({ directNonzero }) => directNonzero)
  );
  const directWindows = maximum(
    media.map(({ directWindows }) => directWindows)
  );
  const bridgeMedia = media.filter(
    ({ pcmBridgeState }) => pcmBridgeState &&
      pcmBridgeState !== "idle"
  );
  const bridgeErrors = bridgeMedia.filter(
    ({ pcmBridgeState }) => pcmBridgeState === "error"
  );
  const bridgeStreaming = bridgeMedia.filter(
    ({ pcmBridgeState }) => pcmBridgeState === "streaming"
  );
  const pcmChunks = maximum(
    bridgeMedia.map(({ pcmChunks }) => pcmChunks)
  );
  const pcmFrames = maximum(
    bridgeMedia.map(({ pcmFrames }) => pcmFrames)
  );
  const pcmBridgeMethod =
    bridgeMedia.find(({ pcmBridgeMethod }) => pcmBridgeMethod)
      ?.pcmBridgeMethod || "";
  const pcmBridgeError =
    bridgeErrors.find(({ pcmBridgeError }) => pcmBridgeError)
      ?.pcmBridgeError || "";
  const pcmBridgeNote =
    bridgeMedia.find(({ pcmBridgeNote }) => pcmBridgeNote)
      ?.pcmBridgeNote || "";
  const expectedFrames = 1 + document.querySelectorAll("iframe").length;
  const coverageComplete = frames.length >= expectedFrames;
  const allFramesApplied =
    frames.length > 0 &&
    frames.every((frame) => frame.desiredMode === requestedMode);
  return {
    desiredMode: requestedMode,
    frames,
    expectedFrames,
    coverageComplete,
    contexts,
    mediaCount: media.length,
    playingMedia,
    uncoveredPlaying,
    attachedMedia,
    graphErrors,
    contextErrors,
    directRms,
    directPeak,
    directNonzero,
    directWindows,
    pcmBridgeState: bridgeErrors.length > 0
      ? "error"
      : bridgeStreaming.length > 0
        ? "streaming"
        : bridgeMedia[0]?.pcmBridgeState || "idle",
    pcmBridgeMethod,
    pcmBridgeNote,
    pcmBridgeError,
    pcmChunks,
    pcmFrames,
    directHasPcm:
      attachedMedia > 0 &&
      directWindows >= 3 &&
      directPeak > 0.0001 &&
      directNonzero > 0.001,
    contextSetSinkSupported:
      frames.some((frame) => frame.contextSetSinkSupported),
    mediaSetSinkSupported:
      frames.some((frame) => frame.mediaSetSinkSupported),
    trackedSilent:
      requestedMode === "none" &&
      coverageComplete &&
      allFramesApplied &&
      contexts.length > 0 &&
      noneContexts === contexts.length &&
      uncoveredPlaying === 0 &&
      contextErrors === 0 &&
      graphErrors === 0
  };
}

export function formatFrameDetails(frames) {
  if (frames.length === 0) return "等待原生预注入脚本回报";
  return frames.map((frame) => {
    const contexts = frame.contexts.length
      ? frame.contexts.map((item) =>
        `#${item.id} ${item.state}/${item.sink}` +
        `${item.error ? `/${item.error}` : ""}`
      ).join(", ")
      : "ctx=0";
    const playing =
      frame.media.filter((item) => item.playing).length;
    const graph = frame.media.map((item) =>
      `${item.graphState} rms=${Number(item.directRms || 0).toFixed(5)}` +
      ` pcm=${item.pcmBridgeState || "idle"}` +
      `${item.pcmBridgeMethod ? `/${item.pcmBridgeMethod}` : ""}` +
      `${item.graphError ? ` ${item.graphError}` : ""}`
    ).join(", ") || "graph=none";
    return `${frame.isTop ? "top" : "frame"} ${frame.origin} · ` +
      `${contexts} · media=${frame.media.length}/${playing} · ${graph}`;
  }).join("；");
}

export function frameFingerprint(state) {
  return JSON.stringify({
    desiredMode: state.desiredMode,
    contexts: state.contexts.map(
      ({ id, state: value, sink, error }) => ({ id, value, sink, error })
    ),
    media: state.media.map(
      ({
        id,
        playing,
        graphState,
        graphError,
        pcmBridgeState,
        pcmBridgeMethod,
        pcmBridgeError
      }) => ({
        id,
        playing,
        graphState,
        graphError,
        pcmBridgeState,
        pcmBridgeMethod,
        pcmBridgeError
      })
    ),
    trackedSilent: state.trackedSilent
  });
}

export function formatFrameLog(state) {
  const sinks = state.contexts.map(({ sink }) => sink).join(",") || "none";
  const playing = state.media.filter(({ playing: value }) => value).length;
  const attached =
    state.media.filter(({ graphState }) => graphState === "attached").length;
  const pcm = state.media.find(
    ({ pcmBridgeState }) => pcmBridgeState &&
      pcmBridgeState !== "idle"
  );
  return `FRAME_PROBE ${state.isTop ? "top" : "child"} ` +
    `origin=${state.origin} contexts=${state.contexts.length} ` +
    `sinks=${sinks} media=${state.media.length}/${playing} ` +
    `graphs=${attached} desired=${state.desiredMode} ` +
    `silent=${state.trackedSilent} ` +
    `pcm=${pcm?.pcmBridgeState || "idle"}` +
    `${pcm?.pcmBridgeMethod ? `/${pcm.pcmBridgeMethod}` : ""}`;
}

export function maximum(values) {
  return values.length
    ? Math.max(...values.map((value) => Number(value || 0)))
    : 0;
}
