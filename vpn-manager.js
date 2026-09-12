const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const PROXY_PORT = 17890;
const DEFAULT_SETTINGS = {
  autoConnect: false,
  webrtcProtection: true,
  lastProfileId: "",
  theme: "dark",
  splitMode: "all",
  splitDomains: [],
  desiredConnected: false
};

function createVpnManager({ app, browserSession, sourceDir, onState }) {
  const runtimeDir = path.join(app.getPath("userData"), "VPNRuntime");
  const settingsPath = path.join(app.getPath("userData"), "vpn-settings.json");
  let settings = { ...DEFAULT_SETTINGS };
  let connection = { state: "disconnected", error: "", activeProfileId: "" };
  let timer = null;

  function loadSettings() {
    try {
      settings = { ...DEFAULT_SETTINGS, ...JSON.parse(fs.readFileSync(settingsPath, "utf8")) };
    } catch (_) {
      settings = { ...DEFAULT_SETTINGS };
    }
  }

  function saveSettings() {
    fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
    fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2), "utf8");
  }

  function prepareRuntime() {
    fs.mkdirSync(runtimeDir, { recursive: true });
    fs.cpSync(sourceDir, runtimeDir, { recursive: true, force: true });
  }

  function publish(state, error = "", activeProfileId = connection.activeProfileId || "") {
    connection = { state, error, activeProfileId };
    if (typeof onState === "function") onState({ type: "state", ...connection });
    return { ok: state !== "error", ...connection };
  }

  function nativeMessage(message, timeoutMs = 30000) {
    if (process.platform !== "win32") {
      return Promise.reject(new Error("VPN helper доступен только в Windows-сборке."));
    }
    const helper = path.join(runtimeDir, "VlessXhttpNativeHost.exe");
    return new Promise((resolve, reject) => {
      const child = spawn(helper, [], { cwd: runtimeDir, windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
      const chunks = [];
      let stderr = "";
      let settled = false;
      const timeout = setTimeout(() => finish(new Error("VPN helper не ответил вовремя.")), timeoutMs);

      function finish(error, result) {
        if (settled) return;
        settled = true;
        clearTimeout(timeout);
        if (!child.killed) child.kill();
        if (error) reject(error);
        else resolve(result);
      }

      child.on("error", (error) => finish(error));
      child.stderr.on("data", (data) => { stderr += data.toString("utf8"); });
      child.stdout.on("data", (data) => {
        chunks.push(data);
        const buffer = Buffer.concat(chunks);
        if (buffer.length < 4) return;
        const length = buffer.readUInt32LE(0);
        if (length > 16 * 1024 * 1024) return finish(new Error("Некорректный ответ VPN helper."));
        if (buffer.length < length + 4) return;
        try {
          finish(null, JSON.parse(buffer.subarray(4, length + 4).toString("utf8")));
        } catch (error) {
          finish(error);
        }
      });
      child.on("exit", (code) => {
        if (!settled) finish(new Error(stderr.trim() || `VPN helper завершился с кодом ${code}.`));
      });

      const json = Buffer.from(JSON.stringify(message), "utf8");
      const header = Buffer.alloc(4);
      header.writeUInt32LE(json.length, 0);
      child.stdin.end(Buffer.concat([header, json]));
    });
  }

  function pacScript(mode, domains) {
    const list = JSON.stringify(domains);
    const decision = mode === "only"
      ? 'if(match)return proxy;return "DIRECT";'
      : 'if(match)return "DIRECT";return proxy;';
    return `function FindProxyForURL(url,host){var d=${list},proxy="PROXY 127.0.0.1:${PROXY_PORT}",match=false;if(isPlainHostName(host)||host==="localhost"||host==="127.0.0.1")return "DIRECT";for(var i=0;i<d.length;i++){if(host===d[i]||dnsDomainIs(host,"."+d[i])){match=true;break;}}${decision}}`;
  }

  async function enableProxy() {
    const domains = Array.isArray(settings.splitDomains) ? settings.splitDomains : [];
    if (settings.splitMode === "all" || domains.length === 0) {
      await browserSession.setProxy({
        mode: "fixed_servers",
        proxyRules: `http://127.0.0.1:${PROXY_PORT}`,
        proxyBypassRules: "<local>;localhost;127.0.0.1;[::1]"
      });
    } else {
      const pac = Buffer.from(pacScript(settings.splitMode, domains), "utf8").toString("base64");
      await browserSession.setProxy({ mode: "pac_script", pacScript: `data:application/x-ns-proxy-autoconfig;base64,${pac}` });
    }
    await browserSession.closeAllConnections();
  }

  async function disableProxy() {
    await browserSession.setProxy({ mode: "direct" });
    await browserSession.closeAllConnections();
  }

  async function connect(profileId) {
    settings.desiredConnected = true;
    settings.lastProfileId = profileId;
    saveSettings();
    publish("connecting", "", profileId);
    try {
      const result = await nativeMessage({ action: "connect", profileId }, 45000);
      if (!result?.ok) throw new Error(result?.error || "Не удалось запустить Xray.");
      await enableProxy();
      return publish("connected", "", profileId);
    } catch (error) {
      settings.desiredConnected = false;
      saveSettings();
      await disableProxy().catch(() => {});
      return publish("error", error.message, profileId);
    }
  }

  async function disconnect() {
    settings.desiredConnected = false;
    saveSettings();
    publish("disconnecting");
    await disableProxy().catch(() => {});
    try {
      const result = await nativeMessage({ action: "disconnect" });
      return publish("disconnected", result?.ok === false ? result.error || "" : "", "");
    } catch (error) {
      return publish("disconnected", error.message, "");
    }
  }

  async function status(recover = false) {
    try {
      const result = await nativeMessage({ action: "status" });
      if (result?.ok && result.connected) {
        await enableProxy();
        return publish("connected", "", result.activeProfileId || settings.lastProfileId);
      }
      if (recover && settings.desiredConnected && settings.lastProfileId) return connect(settings.lastProfileId);
      await disableProxy().catch(() => {});
      return publish("disconnected", "", "");
    } catch (error) {
      if (!settings.desiredConnected) await disableProxy().catch(() => {});
      return publish("error", error.message, settings.lastProfileId);
    }
  }

  async function updateSettings(next) {
    settings = { ...settings, ...(next || {}) };
    saveSettings();
    if (connection.state === "connected" && (Object.hasOwn(next || {}, "splitMode") || Object.hasOwn(next || {}, "splitDomains"))) {
      await enableProxy();
    }
    return { ok: true };
  }

  async function send(action, payload = {}) {
    switch (action) {
      case "status": return status(false);
      case "connect": return connect(payload.profileId);
      case "switchProfile": return connect(payload.profileId);
      case "disconnect": return disconnect();
      case "profiles": return nativeMessage({ action: "list_profiles" });
      case "saveProfile": return nativeMessage({ action: "save_profile", id: payload.id || "", name: payload.name, country: payload.country || "", url: payload.url || "" });
      case "deleteProfile": return nativeMessage({ action: "delete_profile", id: payload.id });
      case "latency": return nativeMessage({ action: "latency", id: payload.id });
      case "checkIp": return nativeMessage({ action: "check_ip" });
      case "settings": return updateSettings(payload.settings);
      case "updateXray": return { ok: true, version: "v26.3.27", message: "В сборку уже включён Xray v26.3.27." };
      default: return { ok: false, error: "Неизвестная VPN-команда." };
    }
  }

  async function initialize() {
    loadSettings();
    prepareRuntime();
    if (process.platform !== "win32") return;
    if (settings.autoConnect && settings.lastProfileId) await connect(settings.lastProfileId);
    else await status(false);
    timer = setInterval(() => status(true).catch(() => {}), 30000);
  }

  function destroy() {
    if (timer) clearInterval(timer);
  }

  return {
    initialize,
    destroy,
    send,
    getSettings: () => ({ ...settings }),
    setSettings: updateSettings,
    getState: () => ({ ...connection })
  };
}

module.exports = { createVpnManager };
