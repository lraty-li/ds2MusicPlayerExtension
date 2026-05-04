let mediaRecorder = null;
let capturedStream = null;
let chunks = [];
let stopTimer = 0;
let activeFileName = "ds2-tab-audio.webm";

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") {
    return false;
  }

  if (message.type === "start-recording") {
    startRecording(message)
      .then(() => sendResponse({ ok: true }))
      .catch((error) => {
        reportError(error);
        sendResponse({ ok: false, error: String(error) });
      });
    return true;
  }

  if (message.type === "stop-recording") {
    stopRecording();
    sendResponse({ ok: true });
    return true;
  }

  return false;
});

async function startRecording(message) {
  stopRecording();
  chunks = [];

  capturedStream = await navigator.mediaDevices.getUserMedia({
    audio: {
      mandatory: {
        chromeMediaSource: "tab",
        chromeMediaSourceId: message.streamId
      }
    },
    video: false
  });

  activeFileName = message.fileName || "ds2-tab-audio.webm";
  const options = pickRecorderOptions();
  mediaRecorder = new MediaRecorder(capturedStream, options);
  mediaRecorder.ondataavailable = (event) => {
    if (event.data && event.data.size > 0) {
      chunks.push(event.data);
    }
  };
  mediaRecorder.onerror = (event) => reportError(event.error || event);
  mediaRecorder.onstop = () => finishRecording();
  mediaRecorder.start(1000);

  stopTimer = setTimeout(() => stopRecording(), message.durationMs || 5000);
}

function stopRecording() {
  if (stopTimer) {
    clearTimeout(stopTimer);
    stopTimer = 0;
  }

  if (mediaRecorder && mediaRecorder.state !== "inactive") {
    mediaRecorder.stop();
    return;
  }

  cleanupStream();
}

async function finishRecording() {
  const mimeType = chunks[0] ? chunks[0].type : "audio/webm";
  const recording = new Blob(chunks, { type: mimeType });

  try {
    if (!recording.size) {
      throw new Error("Recording produced zero bytes.");
    }

    const dataUrl = await blobToDataUrl(recording);
    chrome.runtime.sendMessage({
      type: "recording-finished",
      bytes: recording.size,
      dataUrl,
      fileName: activeFileName
    });
  } catch (error) {
    reportError(error);
  } finally {
    cleanupStream();
  }
}

function cleanupStream() {
  if (capturedStream) {
    for (const track of capturedStream.getTracks()) {
      track.stop();
    }
  }

  capturedStream = null;
  mediaRecorder = null;
  chunks = [];
}

function pickRecorderOptions() {
  const candidates = [
    "audio/webm;codecs=opus",
    "audio/webm"
  ];

  for (const mimeType of candidates) {
    if (MediaRecorder.isTypeSupported(mimeType)) {
      return { mimeType };
    }
  }

  return {};
}

function blobToDataUrl(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(reader.error);
    reader.onload = () => resolve(reader.result);
    reader.readAsDataURL(blob);
  });
}

function reportError(error) {
  cleanupStream();
  chrome.runtime.sendMessage({
    type: "recording-error",
    error: String(error)
  });
}
