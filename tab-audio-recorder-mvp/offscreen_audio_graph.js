let capturedStream = null;
let audioContext = null;
let sourceNode = null;
let workletNode = null;
let audioGraphSampleRate = 48000;

async function startAudioGraph(message, onAudioChunk) {
  capturedStream = await captureTabAudioStream(message);
  audioContext = new AudioContext({ sampleRate: audioGraphSampleRate });
  audioGraphSampleRate = audioContext.sampleRate;
  await audioContext.audioWorklet.addModule("pcm-worklet.js");

  sourceNode = audioContext.createMediaStreamSource(capturedStream);
  workletNode = new AudioWorkletNode(audioContext, "pcm-chunk-worklet", {
    numberOfInputs: 1,
    numberOfOutputs: 0,
    channelCount: 2
  });
  workletNode.port.onmessage = (event) => onAudioChunk(event.data);
  sourceNode.connect(workletNode);
}

function stopAudioGraph() {
  if (workletNode) {
    workletNode.port.onmessage = null;
    workletNode.disconnect();
  }
  if (sourceNode) {
    sourceNode.disconnect();
  }
  if (audioContext) {
    try {
      audioContext.close();
    } catch (_) {
    }
  }
  if (capturedStream) {
    for (const track of capturedStream.getTracks()) {
      track.stop();
    }
  }

  capturedStream = null;
  audioContext = null;
  sourceNode = null;
  workletNode = null;
}

function isAudioGraphActive() {
  return !!capturedStream;
}

function getAudioGraphSampleRate() {
  return audioGraphSampleRate;
}
