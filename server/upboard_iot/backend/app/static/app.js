const state = {
  user: null,
  devices: [],
  selectedSn: null,
  source: null,
  gpsMap: null,
  gpsLayer: null,
  historyRows: [],
  resizeTimer: null,
};

const $ = (id) => document.getElementById(id);

function fmtTime(value) {
  if (!value) return "-";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "-";
  return date.toLocaleString();
}

function isOnline(value) {
  if (!value) return false;
  const ageMs = Date.now() - new Date(value).getTime();
  return ageMs >= 0 && ageMs < 90_000;
}

async function api(path, options = {}) {
  const res = await fetch(path, {
    credentials: "same-origin",
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || `${res.status}`);
  }
  return res.json();
}

function showLogin() {
  $("loginView").classList.remove("hidden");
  $("appView").classList.add("hidden");
}

function showApp() {
  $("loginView").classList.add("hidden");
  $("appView").classList.remove("hidden");
  $("sessionLine").textContent = `${state.user.username} / ${state.user.role}`;
}

async function checkSession() {
  const me = await api("/api/me");
  if (!me.authenticated) {
    showLogin();
    return;
  }
  state.user = me;
  showApp();
  await loadDevices();
  connectStream();
}

async function login(event) {
  event.preventDefault();
  $("loginError").textContent = "";
  try {
    const username = $("username").value.trim();
    const password = $("password").value;
    const user = await api("/api/auth/login", {
      method: "POST",
      body: JSON.stringify({ username, password }),
    });
    state.user = user;
    showApp();
    await loadDevices();
    connectStream();
  } catch (err) {
    $("loginError").textContent = "登录失败，请检查账号和密码";
  }
}

async function logout() {
  await api("/api/auth/logout", { method: "POST", body: "{}" }).catch(() => {});
  if (state.source) state.source.close();
  state.source = null;
  state.user = null;
  state.devices = [];
  state.selectedSn = null;
  showLogin();
}

async function loadDevices() {
  const data = await api("/api/devices");
  state.devices = data.devices || [];
  if (!state.selectedSn && state.devices.length) {
    state.selectedSn = state.devices[0].sn;
  }
  renderDevices();
  renderSummary();
  if (state.selectedSn) {
    await loadDetail(state.selectedSn);
  }
}

function renderSummary() {
  const online = state.devices.filter((d) => isOnline(d.last_seen_at)).length;
  $("totalDevices").textContent = state.devices.length;
  $("onlineDevices").textContent = online;
  $("deviceCount").textContent = state.devices.length;
  $("selectedSn").textContent = state.selectedSn || "-";
  const selected = state.devices.find((d) => d.sn === state.selectedSn);
  $("lastSeen").textContent = fmtTime(selected?.last_seen_at);
}

function renderDevices() {
  const box = $("devicesTable");
  box.innerHTML = "";
  for (const device of state.devices) {
    const online = isOnline(device.last_seen_at);
    const row = document.createElement("button");
    row.className = `device-row ${device.sn === state.selectedSn ? "active" : ""}`;
    row.type = "button";
    row.setAttribute("aria-pressed", device.sn === state.selectedSn ? "true" : "false");
    row.innerHTML = `
      <div>
        <strong>${escapeHtml(device.sn)}</strong>
        <span class="device-meta">${fmtTime(device.last_seen_at)}<br>seq ${device.latest_seq ?? "-"}</span>
      </div>
      <span class="pill ${online ? "online" : "offline"}">
        ${online ? "在线" : "离线"}
      </span>
    `;
    row.addEventListener("click", async () => {
      state.selectedSn = device.sn;
      renderDevices();
      renderSummary();
      await loadDetail(device.sn);
    });
    box.appendChild(row);
  }
}

async function loadDetail(sn) {
  $("detailTitle").textContent = sn;
  const [latest, history, gps, events] = await Promise.all([
    api(`/api/devices/${encodeURIComponent(sn)}/latest`),
    api(`/api/devices/${encodeURIComponent(sn)}/telemetry?limit=200`),
    api(`/api/devices/${encodeURIComponent(sn)}/gps?limit=100`),
    api(`/api/devices/${encodeURIComponent(sn)}/events?limit=100`),
  ]);
  renderLatest(latest.latest);
  renderHistory(history.telemetry || []);
  renderGps(gps.gps || []);
  renderEvents(events.events || []);
}

function renderLatest(row) {
  const selected = state.devices.find((d) => d.sn === state.selectedSn);
  const online = isOnline(selected?.last_seen_at);
  $("detailStatus").className = `pill ${online ? "online" : "offline"}`;
  $("detailStatus").textContent = online ? "在线" : "离线";
  if (!row) {
    $("rssiValue").textContent = "-";
    $("rssiMeta").textContent = "等待上报";
    $("gpsValue").textContent = "-";
    $("gpsMeta").textContent = "等待定位";
    $("bat24Value").textContent = "-";
    $("bat24Meta").textContent = "等待上报";
    $("v12Value").textContent = "-";
    $("v12Meta").textContent = "等待上报";
    $("outputs").innerHTML = "";
    $("ntcGrid").innerHTML = "";
    $("rawPayload").textContent = "{}";
    return;
  }
  $("rssiValue").textContent = row.rssi ?? "-";
  $("gpsValue").textContent = row.latitude == null || row.longitude == null
    ? "无有效定位"
    : `${Number(row.latitude).toFixed(6)}, ${Number(row.longitude).toFixed(6)}`;
  $("gpsMeta").textContent = row.latitude == null || row.longitude == null
    ? "等待采样板 fix"
    : fmtTime(row.received_at);
  $("bat24Value").textContent = row.bat24_v == null ? "-" : `${Number(row.bat24_v).toFixed(2)} V`;
  $("bat24Meta").textContent = row.bat24_i == null ? "电池侧" : `${Number(row.bat24_i).toFixed(3)} A`;
  $("v12Value").textContent = row.v12_v == null ? "-" : `${Number(row.v12_v).toFixed(2)} V`;
  $("v12Meta").textContent = "低压侧";
  renderOutputs(row);
  renderNtc(row.ntc_raw || []);
  $("rawPayload").textContent = JSON.stringify(row.raw_json || {}, null, 2);
}

function renderOutputs(row) {
  const names = [
    ["BOOST", row.boost_on],
    ["LOAD", row.load_on],
    ["FAN", row.fan_on],
    ["PUMP", row.pump_on],
    ["COMP", row.compressor_on],
  ];
  $("outputs").innerHTML = names
    .map(([name, value]) => `<div class="output-chip ${value ? "on" : "off"}">${name}<br><strong>${value ? "ON" : "OFF"}</strong></div>`)
    .join("");
}

function renderNtc(values) {
  const list = Array.isArray(values) ? values : [];
  $("ntcGrid").innerHTML = Array.from({ length: 8 }, (_, idx) => {
    const value = list[idx] ?? "-";
    return `<div class="ntc-cell">NTC${idx + 1}<strong>${value}</strong></div>`;
  }).join("");
}

function renderHistory(rows) {
  state.historyRows = Array.isArray(rows) ? rows : [];
  const points = rows
    .filter((row) => row.bat24_v != null)
    .map((row) => ({ x: new Date(row.received_at).getTime(), y: Number(row.bat24_v) }));
  drawLineChart($("voltageChart"), points);
}

function renderGps(rows) {
  const points = rows
    .filter((row) => row.latitude != null && row.longitude != null)
    .map((row) => ({
      time: row.received_at,
      lat: Number(row.latitude),
      lon: Number(row.longitude),
      fix: row.gps_fix,
    }))
    .filter((point) => Number.isFinite(point.lat) && Number.isFinite(point.lon));

  renderGpsMap(points);
  const latest = points[points.length - 1];
  if (latest) {
    $("gpsValue").textContent = `${latest.lat.toFixed(6)}, ${latest.lon.toFixed(6)}`;
    $("gpsMeta").textContent = fmtTime(latest.time);
  } else {
    $("gpsValue").textContent = "无有效定位";
    $("gpsMeta").textContent = "等待采样板 fix";
  }

  $("gpsList").innerHTML = points.length
    ? points.slice(-30).reverse().map((point) => `
      <button class="list-line gps-point" type="button" data-lat="${point.lat}" data-lon="${point.lon}">
        ${fmtTime(point.time)}<br />
        ${point.lat.toFixed(6)}, ${point.lon.toFixed(6)}
      </button>
    `).join("")
    : '<div class="list-line">暂无 GPS 点</div>';

  document.querySelectorAll(".gps-point").forEach((node) => {
    node.addEventListener("click", () => {
      if (!state.gpsMap) return;
      const lat = Number(node.dataset.lat);
      const lon = Number(node.dataset.lon);
      if (Number.isFinite(lat) && Number.isFinite(lon)) {
        state.gpsMap.setView([lat, lon], Math.max(state.gpsMap.getZoom(), 15));
      }
    });
  });
}

function ensureGpsMap() {
  if (state.gpsMap || !window.L) return state.gpsMap;
  const map = L.map("gpsMap", {
    zoomControl: true,
    attributionControl: true,
  }).setView([22.3, 114.1], 10);
  L.tileLayer("https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap contributors &copy; CARTO",
  }).addTo(map);
  state.gpsLayer = L.layerGroup().addTo(map);
  state.gpsMap = map;
  return map;
}

function renderGpsMap(points) {
  const map = ensureGpsMap();
  if (!map || !state.gpsLayer) return;
  state.gpsLayer.clearLayers();

  setTimeout(() => map.invalidateSize(), 0);
  if (!points.length) {
    map.setView([22.3, 114.1], 10);
    return;
  }

  const latLngs = points.map((point) => [point.lat, point.lon]);
  if (latLngs.length > 1) {
    L.polyline(latLngs, { color: "#7dd3fc", weight: 3, opacity: 0.9 }).addTo(state.gpsLayer);
  }

  const latest = points[points.length - 1];
  const markerIcon = L.divIcon({
    className: "gps-marker",
    html: "<span></span>",
    iconSize: [30, 30],
    iconAnchor: [15, 15],
  });
  L.marker([latest.lat, latest.lon], { icon: markerIcon })
    .bindPopup(`${fmtTime(latest.time)}<br>${latest.lat.toFixed(6)}, ${latest.lon.toFixed(6)}`)
    .addTo(state.gpsLayer);

  if (latLngs.length === 1) {
    map.setView(latLngs[0], 15);
  } else {
    map.fitBounds(L.latLngBounds(latLngs), { padding: [28, 28], maxZoom: 16 });
  }
}

function renderEvents(rows) {
  $("eventsList").innerHTML = rows.length
    ? rows.slice(-30).reverse().map((row) => `
      <div class="list-line">
        ${fmtTime(row.received_at)} | ${escapeHtml(row.level)} | ${escapeHtml(row.event_type)}<br />
        ${escapeHtml(row.message)}
      </div>
    `).join("")
    : '<div class="list-line">暂无事件</div>';
}

function drawLineChart(canvas, points) {
  const prepared = prepareCanvas(canvas);
  const { ctx, width, height } = prepared;
  const root = getComputedStyle(document.documentElement);
  const line = root.getPropertyValue("--accent-strong").trim() || "#7dd3fc";
  const grid = root.getPropertyValue("--line").trim() || "#223140";
  const text = root.getPropertyValue("--muted").trim() || "#91a2b4";
  const fill = root.getPropertyValue("--bg-soft").trim() || "#0c131c";

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = fill;
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = grid;
  ctx.lineWidth = 1;
  ctx.font = '12px "SFMono-Regular", Consolas, monospace';
  for (let i = 0; i < 5; i++) {
    const y = 28 + i * ((height - 56) / 4);
    ctx.beginPath();
    ctx.moveTo(56, y);
    ctx.lineTo(width - 18, y);
    ctx.stroke();
  }
  if (!points.length) {
    ctx.fillStyle = text;
    ctx.fillText("暂无数据", 56, 48);
    return;
  }
  const minY = Math.min(...points.map((p) => p.y));
  const maxY = Math.max(...points.map((p) => p.y));
  const spanY = Math.max(0.1, maxY - minY);
  const left = 58;
  const right = width - 20;
  const top = 28;
  const bottom = height - 32;
  const coords = points.map((point, idx) => ({
    x: left + (idx / Math.max(1, points.length - 1)) * (right - left),
    y: bottom - ((point.y - minY) / spanY) * (bottom - top),
  }));

  const gradient = ctx.createLinearGradient(0, top, 0, bottom);
  gradient.addColorStop(0, "rgba(125, 211, 252, 0.28)");
  gradient.addColorStop(1, "rgba(125, 211, 252, 0)");
  ctx.beginPath();
  coords.forEach((point, idx) => {
    if (idx === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.lineTo(coords[coords.length - 1].x, bottom);
  ctx.lineTo(coords[0].x, bottom);
  ctx.closePath();
  ctx.fillStyle = gradient;
  ctx.fill();

  ctx.strokeStyle = line;
  ctx.lineWidth = 2;
  ctx.beginPath();
  coords.forEach((point, idx) => {
    if (idx === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.stroke();

  ctx.fillStyle = text;
  ctx.fillText(`${maxY.toFixed(2)}V`, 10, top + 4);
  ctx.fillText(`${minY.toFixed(2)}V`, 10, bottom);
}

function prepareCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(320, Math.floor(canvas.clientWidth || canvas.width));
  const height = Math.max(220, Math.floor(canvas.clientHeight || canvas.height));
  const targetWidth = Math.floor(width * dpr);
  const targetHeight = Math.floor(height * dpr);
  if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
    canvas.width = targetWidth;
    canvas.height = targetHeight;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, width, height };
}

function connectStream() {
  if (state.source) state.source.close();
  const source = new EventSource("/api/stream", { withCredentials: true });
  state.source = source;
  source.addEventListener("open", () => {
    $("streamState").textContent = "SSE 已连接";
    $("streamState").className = "pill online";
  });
  source.addEventListener("error", () => {
    $("streamState").textContent = "SSE 重连中";
    $("streamState").className = "pill offline";
  });
  source.addEventListener("telemetry", async (event) => {
    const msg = JSON.parse(event.data);
    await loadDevices();
    if (msg.sn === state.selectedSn) {
      await loadDetail(msg.sn);
    }
  });
  source.addEventListener("event", async (event) => {
    const msg = JSON.parse(event.data);
    await loadDevices();
    if (msg.sn === state.selectedSn) {
      await loadDetail(msg.sn);
    }
  });
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

$("loginForm").addEventListener("submit", login);
$("logoutBtn").addEventListener("click", logout);
$("refreshBtn").addEventListener("click", loadDevices);
window.addEventListener("resize", () => {
  clearTimeout(state.resizeTimer);
  state.resizeTimer = setTimeout(() => renderHistory(state.historyRows), 120);
});

checkSession().catch(() => showLogin());
