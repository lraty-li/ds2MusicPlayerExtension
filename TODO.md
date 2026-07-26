# TODO

- [ ] 调整 Spotify helper 的 Widevine 预检：当前
  `prepareWidevine()` 超时或临时失败后会终止本次初始化，且不进入现有重试
  流程。应改为可重试或非阻断路径，并验证冷启动及 CDM 尚未预热的环境。
