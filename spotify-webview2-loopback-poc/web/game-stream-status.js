const byId = (id) => document.getElementById(id);
let metrics = {};
let lastVerdict = "";
let logMessage = () => {};
let installed = false;

export function initializeGameStreamStatus(log) {
  if (installed) return;
  installed = true;
  logMessage = log;
  byId("probe-pause").addEventListener(
    "click",
    () => requestProbeControl("pause")
  );
  byId("probe-resume").addEventListener(
    "click",
    () => requestProbeControl("resume")
  );
  window.chrome?.webview?.addEventListener("message", ({ data }) => {
    if (data?.type !== "game-stream-metrics") return;
    metrics = data;
    render();
  });
}

function requestProbeControl(command) {
  window.chrome?.webview?.postMessage(`probe-control:${command}`);
  logMessage(`已请求 standalone probe 反向发送 ${command}`);
}

function render() {
  setText("game-stream-endpoint", metrics.endpoint || "ws://127.0.0.1:47832");
  setText(
    "game-stream-connection",
    metrics.connected
      ? `已连接 · 第 ${metrics.connections || 1} 次`
      : `等待接收探针 · 尝试 ${metrics.connectAttempts || 0} 次`
  );
  setText(
    "game-stream-sent",
    `${number(metrics.packetsSent)} 包 · ` +
    `${number(metrics.framesSent)} 帧 · ` +
    `${formatBytes(metrics.pcmBytesSent)}`
  );
  setText(
    "game-stream-queue",
    `当前 ${number(metrics.queueDepth)} · ` +
    `峰值 ${number(metrics.maxQueueDepth)}`
  );
  setText(
    "game-stream-errors",
    `丢包 ${number(metrics.droppedPackets)} · ` +
    `发送错误 ${number(metrics.sendErrors)} · ` +
    `无效 ${number(metrics.invalidChunks)}`
  );
  setText(
    "game-stream-controls",
    `暂停 ${number(metrics.pauseCommands)} · ` +
    `恢复 ${number(metrics.resumeCommands)}`
  );
  renderVerdict();
}

function renderVerdict() {
  if (!metrics.connected) {
    verdict(
      "等待独立游戏协议接收探针",
      "idle",
      metrics.error || "本阶段不连接游戏；先启动 standalone probe。"
    );
    return;
  }
  const failures =
    number(metrics.droppedPackets) +
    number(metrics.sendErrors) +
    number(metrics.invalidChunks);
  if (failures > 0) {
    verdict(
      "游戏协议流发生错误",
      "fail",
      `丢包 ${number(metrics.droppedPackets)}，` +
      `发送错误 ${number(metrics.sendErrors)}，` +
      `无效 ${number(metrics.invalidChunks)}`
    );
    return;
  }
  if (number(metrics.packetsSent) < 100) {
    verdict(
      "游戏协议已连接，正在积累实时窗口",
      "running",
      "至少连续发送 100 个 10 ms 数据包后判定。"
    );
    return;
  }
  if (number(metrics.framesSent) ===
      number(metrics.packetsSent) * 480) {
    verdict(
      "发送端通过：游戏协议 PCM 连续输出",
      "pass",
      "48 kHz 双声道 PCM16 已按 480 帧/包发送。"
    );
    return;
  }
  verdict(
    "游戏协议帧数不符合数据包契约",
    "fail",
    "累计帧数不等于数据包数 × 480。"
  );
}

function verdict(title, kind, detail) {
  const element = byId("game-stream-verdict");
  element.textContent = title;
  element.className = `verdict ${kind}`;
  setText("game-stream-detail", detail);
  const fingerprint = `${kind}:${title}`;
  if (lastVerdict !== fingerprint) {
    lastVerdict = fingerprint;
    logMessage(`GAME_STREAM ${title}：${detail}`);
  }
}

function number(value) {
  return Number(value || 0);
}

function formatBytes(value) {
  const bytes = number(value);
  return bytes >= 1048576
    ? `${(bytes / 1048576).toFixed(2)} MiB`
    : `${(bytes / 1024).toFixed(1)} KiB`;
}

function setText(id, value) {
  byId(id).textContent = String(value);
}
