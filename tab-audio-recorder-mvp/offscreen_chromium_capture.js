async function captureTabAudioStream(message) {
  if (!message || !message.streamId) {
    throw new Error("missing Chromium tabCapture stream id");
  }

  return navigator.mediaDevices.getUserMedia({
    audio: {
      mandatory: {
        chromeMediaSource: "tab",
        chromeMediaSourceId: message.streamId
      }
    },
    video: false
  });
}
