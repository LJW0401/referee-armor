const state = { connected: false, polling: null };
const select = document.querySelector("#port-select");
const message = document.querySelector("#message");
const connection = document.querySelector("#connection-state");
const connectButton = document.querySelector("#connect");
const disconnectButton = document.querySelector("#disconnect");
const applyColorButton = document.querySelector("#apply-color");
const sliders = ["red", "green", "blue", "brightness"].map(component => ({
  input: document.querySelector(`#${component}-slider`),
  output: document.querySelector(`#${component}-value`),
}));
const colorPreview = document.querySelector("#color-preview");

async function request(path, options = {}) {
  const response = await fetch(path, options);
  if (response.status === 204) return null;
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
  return payload;
}

function setMessage(text = "", error = false) {
  message.textContent = text;
  message.className = error ? "error" : "";
}

function renderList(target, values) {
  target.replaceChildren();
  for (const [key, value] of values) {
    const term = document.createElement("dt"); term.textContent = key;
    const detail = document.createElement("dd"); detail.textContent = String(value);
    target.append(term, detail);
  }
}

function render(snapshot, synchronizeColor = false) {
  const { port, device, status } = snapshot;
  state.connected = true;
  connection.textContent = `已连接 · ${port}`;
  connection.className = "state connected";
  connectButton.disabled = true;
  disconnectButton.disabled = false;
  applyColorButton.disabled = false;
  renderList(document.querySelector("#device-info"), [
    ["设备 ID", device.device_id], ["固件", device.firmware_version],
    ["协议", `v${device.protocol_version}`], ["能力位图", `0x${device.capabilities.toString(16).padStart(8, "0")}`],
  ]);
  const weight = status.weight_mg === null ? "--" : `${(status.weight_mg / 1000).toFixed(3)} g`;
  document.querySelector("#weight").textContent = weight;
  document.querySelector("#weight-detail").textContent = status.sample_age_ms === null
    ? "HX711 尚无有效样本" : `样本年龄 ${status.sample_age_ms} ms`;
  if (synchronizeColor) setSelectedColorFromRgb(status.led_red, status.led_green, status.led_blue, status.led_brightness_percent);
  renderList(document.querySelector("#runtime-status"), [
    ["运行时间", `${status.uptime_ms} ms`], ["健康标志", `0x${status.health_flags.toString(16).padStart(4, "0")}`],
    ["WS2812 灯珠", status.led_count], ["灯效", status.active_led_effect],
  ]);
}

function clearConnection() {
  state.connected = false;
  clearInterval(state.polling); state.polling = null;
  connection.textContent = "未连接"; connection.className = "state disconnected";
  connectButton.disabled = false; disconnectButton.disabled = true;
  applyColorButton.disabled = true;
  document.querySelector("#weight").textContent = "--";
  document.querySelector("#weight-detail").textContent = "等待有效样本";
}

async function refreshPorts() {
  try {
    const { ports } = await request("/api/ports");
    select.replaceChildren();
    if (!ports.length) select.add(new Option("未发现串口", ""));
    for (const port of ports) select.add(new Option(`${port.device} — ${port.description}`, port.device));
    setMessage();
  } catch (error) { setMessage(error.message, true); }
}

async function pollStatus() {
  if (!state.connected) return;
  try { render(await request("/api/status")); }
  catch (error) { setMessage(`状态读取失败：${error.message}`, true); clearConnection(); }
}

document.querySelector("#refresh-ports").addEventListener("click", refreshPorts);
connectButton.addEventListener("click", async () => {
  if (!select.value) return setMessage("请先选择一个串口", true);
  connectButton.disabled = true; setMessage("正在执行协议握手…");
  try {
    render(await request("/api/connect", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ port: select.value }) }), true);
    setMessage("握手成功，正在读取状态。"); state.polling = setInterval(pollStatus, 1000);
  } catch (error) { setMessage(`连接失败：${error.message}`, true); clearConnection(); }
});
disconnectButton.addEventListener("click", async () => {
  await request("/api/disconnect", { method: "POST" }); clearConnection(); setMessage("已断开串口。");
});

function selectedColor() {
  return sliders.map(slider => Number.parseInt(slider.input.value, 10));
}

function updateColorPreview() {
  const [red, green, blue, brightnessPercent] = selectedColor();
  const maximum = Math.max(red, green, blue);
  const previewComponents = maximum === 0 ? [0, 0, 0] : [red, green, blue].map(value => Math.round(value / maximum * brightnessPercent / 100 * 255));
  const hex = `#${previewComponents.map(value => value.toString(16).padStart(2, "0")).join("").toUpperCase()}`;
  sliders.forEach((slider, index) => { slider.output.textContent = index === 3 ? `${slider.input.value}%` : slider.input.value; });
  colorPreview.textContent = hex;
  colorPreview.style.backgroundColor = hex;
  colorPreview.style.color = red * 299 + green * 587 + blue * 114 > 150000 ? "#10151b" : "#ecf2f8";
}

function setSelectedColor(hex) {
  const color = hex.replace("#", "");
  sliders.slice(0, 3).forEach((slider, index) => { slider.input.value = Number.parseInt(color.slice(index * 2, index * 2 + 2), 16); });
  sliders[3].input.value = 100;
  updateColorPreview();
}

function setSelectedColorFromRgb(red, green, blue, brightnessPercent) {
  sliders.forEach((slider, index) => { slider.input.value = [red, green, blue, brightnessPercent][index]; });
  updateColorPreview();
}

async function applyColor() {
  if (!state.connected) return setMessage("请先连接 ESP32", true);
  const [red, green, blue, brightness_percent] = selectedColor();
  try {
    render(await request("/api/led-color", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ red, green, blue, brightness_percent }) }));
    setMessage(`已将左右灯条设置为 ${colorPreview.textContent}。`);
  } catch (error) { setMessage(`设置灯光失败：${error.message}`, true); }
}

document.querySelectorAll("[data-color]").forEach(button => button.addEventListener("click", () => {
  setSelectedColor(button.dataset.color); applyColor();
}));
sliders.forEach(slider => slider.input.addEventListener("input", updateColorPreview));
applyColorButton.addEventListener("click", applyColor);

updateColorPreview();
refreshPorts();
