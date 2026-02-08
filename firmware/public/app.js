const MAX_DEVICES = 15;
const SAVE_DEBOUNCE_MS = 600;

const elements = {
  version: document.getElementById("version"),
  statusPill: document.getElementById("status-pill"),
  deviceCount: document.getElementById("device-count"),
  saveStatus: document.getElementById("save-status"),
  addButton: document.getElementById("add-device"),
  tableBody: document.getElementById("devices-body"),
  emptyState: document.getElementById("empty-state"),
  drawer: document.getElementById("device-drawer"),
  drawerBackdrop: document.getElementById("drawer-backdrop"),
  drawerTitle: document.getElementById("drawer-title"),
  drawerClose: document.getElementById("drawer-close"),
  form: document.getElementById("device-form"),
  formEnabled: document.getElementById("form-enabled"),
  formAddress: document.getElementById("form-address"),
  formName: document.getElementById("form-name"),
  formCalibration: document.getElementById("form-calibration"),
  formReversed: document.getElementById("form-reversed"),
  formDelete: document.getElementById("form-delete"),
  formCancel: document.getElementById("form-cancel"),
  otaOpen: document.getElementById("ota-open"),
  otaDrawer: document.getElementById("ota-drawer"),
  otaBackdrop: document.getElementById("ota-backdrop"),
  otaClose: document.getElementById("ota-close"),
  otaCancel: document.getElementById("ota-cancel"),
  otaForm: document.getElementById("ota-form"),
  otaFile: document.getElementById("firmware")
};

const state = {
  config: null,
  status: null,
  statusMap: new Map(),
  rowNodes: [],
  saveTimer: null,
  statusInFlight: false,
  activeDevice: null,
  isAdding: false
};

function setSaveStatus(text, variant) {
  elements.saveStatus.textContent = text;
  elements.saveStatus.classList.remove("ok", "error", "saving");
  if (variant) {
    elements.saveStatus.classList.add(variant);
  }
}

function setStatusPill(isLive) {
  elements.statusPill.textContent = isLive ? "Live" : "Offline";
  elements.statusPill.classList.toggle("offline", !isLive);
}

async function fetchJson(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.json();
}

function normalizeConfig(config) {
  const normalized = {
    format: Number.isFinite(config.format) ? config.format : 1,
    network: config.network || {},
    devices: Array.isArray(config.devices) ? config.devices : []
  };

  normalized.devices = normalized.devices.map((device, idx) => {
    const address = Number.isFinite(device.address) ? device.address : idx + 1;
    return {
      enabled: typeof device.enabled === "boolean" ? device.enabled : true,
      address,
      name: typeof device.name === "string" ? device.name : `Device ${address}`,
      calibration: Number.isFinite(device.calibration) ? device.calibration : 1.0,
      reversed: typeof device.reversed === "boolean" ? device.reversed : false
    };
  });

  normalized.devices.sort((a, b) => a.address - b.address);
  return normalized;
}

function formatMetric(value, decimals = 3) {
  if (!Number.isFinite(value)) {
    return "--";
  }
  const fixed = value.toFixed(decimals);
  return fixed.replace(/\.0+$/, "").replace(/(\.\d*?)0+$/, "$1");
}

function formatVoltage(metrics) {
  if (!metrics) {
    return "--";
  }

  const volts = Number(metrics.volts);
  if (!Number.isFinite(volts)) {
    return "--";
  }

  const voltsLabel = formatMetric(volts, 0);
  return `${voltsLabel} V`;
}

function formatPower(metrics) {
  if (!metrics) {
    return "--";
  }
  const volts = Number(metrics.volts);
  const amps = Number(metrics.amps);
  const pf = Number(metrics.pf);
  if (!Number.isFinite(volts) || !Number.isFinite(amps) || !Number.isFinite(pf)) {
    return "--";
  }

  const watts = volts * amps * pf;
  const wattsLabel = formatMetric(watts, 0);
  if (!Number.isFinite(watts)) {
    return "--";
  }

  if (Math.abs(pf - 1) < 0.005) {
    return `${wattsLabel} W`;
  }

  return `${wattsLabel} W, pf ${formatMetric(pf, 2)}`;
}

function updateVersion() {
  elements.version.textContent = state.status?.version || "--";
}

function updateStatusMap() {
  state.statusMap.clear();
  const devices = state.status?.devices || [];
  devices.forEach((device) => {
    if (device.name) {
      state.statusMap.set(device.name, device);
    }
  });
}

function updateMetrics() {
  state.rowNodes.forEach(({ row, device }) => {
    const name = device?.name ? device.name.trim() : "";
    const metrics = name ? state.statusMap.get(name) : null;
    const voltsText = row.querySelector("[data-volts-text]");
    if (voltsText) {
      voltsText.textContent = formatVoltage(metrics);
    }
    const powerText = row.querySelector("[data-power-text]");
    if (powerText) {
      powerText.textContent = formatPower(metrics);
    }
  });
}

function updateDeviceCount() {
  const count = state.config?.devices.length || 0;
  elements.deviceCount.textContent = `${count}/${MAX_DEVICES}`;
  elements.addButton.disabled = count >= MAX_DEVICES;
}

function setInputError(input, hasError) {
  if (!input) return;
  input.classList.toggle("input-error", hasError);
}

function validateDevice(device) {
  let valid = true;
  const nameValid = device.name && device.name.trim().length > 0;
  const calib = Number(device.calibration);
  const calibValid = Number.isFinite(calib) && calib > 0;
  if (!nameValid) valid = false;
  if (!calibValid) valid = false;
  return valid;
}

function validateConfig() {
  let valid = true;
  const addressCounts = new Map();

  state.config?.devices.forEach((device) => {
    const addr = Number(device.address);
    const addrValid = Number.isInteger(addr) && addr >= 1 && addr <= MAX_DEVICES;
    if (addrValid) {
      addressCounts.set(addr, (addressCounts.get(addr) || 0) + 1);
    } else {
      valid = false;
    }

    if (!validateDevice(device)) {
      valid = false;
    }
  });

  state.config?.devices.forEach((device) => {
    const addr = Number(device.address);
    const duplicate = Number.isInteger(addr) && addressCounts.get(addr) > 1;
    if (duplicate) {
      valid = false;
    }
  });

  return valid;
}

function scheduleSave() {
  if (!state.config) {
    return;
  }

  if (!validateConfig()) {
    setSaveStatus("Fix highlighted fields", "error");
    return;
  }

  setSaveStatus("Unsaved changes", "saving");
  clearTimeout(state.saveTimer);
  state.saveTimer = setTimeout(saveConfig, SAVE_DEBOUNCE_MS);
}

async function saveConfig() {
  if (!state.config) {
    return;
  }

  if (!validateConfig()) {
    setSaveStatus("Fix highlighted fields", "error");
    return;
  }

  setSaveStatus("Saving...", "saving");

  try {
    const response = await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(state.config)
    });

    if (!response.ok) {
      throw new Error(`Save failed: ${response.status}`);
    }

    setSaveStatus("Saved", "ok");
  } catch (error) {
    console.error(error);
    setSaveStatus("Save failed", "error");
  }
}

function createAddressBadge(value) {
  const badge = document.createElement("span");
  badge.className = "address-chip";
  badge.textContent = Number.isFinite(value) ? String(value) : "--";
  return badge;
}

function createRow(device) {
  const row = document.createElement("tr");
  row.classList.toggle("row-disabled", !device.enabled);

  const addressCell = document.createElement("td");
  const addressBadge = createAddressBadge(device.address);
  addressCell.appendChild(addressBadge);
  row.appendChild(addressCell);

  const nameCell = document.createElement("td");
  nameCell.textContent = device.name || "--";
  row.appendChild(nameCell);

  const voltsCell = document.createElement("td");
  voltsCell.setAttribute("data-volts-text", "true");
  voltsCell.textContent = "--";
  row.appendChild(voltsCell);

  const powerCell = document.createElement("td");
  powerCell.className = "metric";
  powerCell.setAttribute("data-power", "true");
  const powerWrap = document.createElement("span");
  powerWrap.className = "power-cell";
  const powerText = document.createElement("span");
  powerText.setAttribute("data-power-text", "true");
  powerText.textContent = "--";
  powerWrap.appendChild(powerText);
  if (device.reversed) {
    const reversedIcon = document.createElement("span");
    reversedIcon.className = "power-icon active";
    reversedIcon.setAttribute("title", "Reversed");
    reversedIcon.innerHTML = "<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\" aria-hidden=\"true\"><path d=\"M12 5a7 7 0 1 1-6.22 10.22\" stroke=\"currentColor\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M4.5 9V5.5H8\" stroke=\"currentColor\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>";
    powerWrap.appendChild(reversedIcon);
  }
  powerCell.appendChild(powerWrap);
  row.appendChild(powerCell);

  const actionCell = document.createElement("td");
  const editButton = document.createElement("button");
  editButton.type = "button";
  editButton.className = "btn-icon-only";
  editButton.innerHTML = "<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M4 20h4l10-10a2.828 2.828 0 1 0-4-4L4 16v4z\" stroke=\"#344054\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M13 7l4 4\" stroke=\"#344054\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>";
  editButton.setAttribute("aria-label", "Edit device");
  actionCell.appendChild(editButton);
  row.appendChild(actionCell);

  editButton.addEventListener("click", () => openDrawer(device));

  return { row, device };
}

function renderTable() {
  elements.tableBody.innerHTML = "";
  state.rowNodes = [];

  if (!state.config || state.config.devices.length === 0) {
    elements.emptyState.classList.remove("hidden");
  } else {
    elements.emptyState.classList.add("hidden");
  }

  state.config?.devices.forEach((device) => {
    const { row } = createRow(device);
    elements.tableBody.appendChild(row);
    state.rowNodes.push({ row, device });
  });

  updateDeviceCount();
  updateMetrics();
}

function findAvailableAddress() {
  const devices = state.config?.devices || [];
  if (devices.length === 0) {
    return 1;
  }
  const maxAddress = Math.max(...devices.map((device) => Number(device.address) || 0));
  const next = maxAddress + 1;
  return next <= MAX_DEVICES ? next : null;
}

function openDrawer(device) {
  state.activeDevice = device;
  state.isAdding = !device;

  if (state.isAdding) {
    const address = findAvailableAddress();
    if (!address) {
      return;
    }
    state.activeDevice = {
      enabled: true,
      address,
      name: "",
      calibration: 1.0,
      reversed: false
    };
  }

  elements.drawerTitle.textContent = state.isAdding ? "Add device" : "Edit device";
  elements.formEnabled.checked = Boolean(state.activeDevice.enabled);
  elements.formAddress.value = String(state.activeDevice.address || "");
  elements.formName.value = state.activeDevice.name || "";
  elements.formCalibration.value = Number.isFinite(state.activeDevice.calibration)
    ? String(state.activeDevice.calibration)
    : "";
  elements.formReversed.checked = Boolean(state.activeDevice.reversed);

  elements.formDelete.disabled = state.isAdding;
  elements.formDelete.style.visibility = state.isAdding ? "hidden" : "visible";

  elements.drawer.classList.add("open");
  elements.drawer.setAttribute("aria-hidden", "false");
  elements.drawerBackdrop.classList.add("open");
  elements.drawerBackdrop.classList.remove("hidden");
}

function closeDrawer() {
  elements.drawer.classList.remove("open");
  elements.drawer.setAttribute("aria-hidden", "true");
  elements.drawerBackdrop.classList.remove("open");
  elements.drawerBackdrop.classList.add("hidden");
  state.activeDevice = null;
  state.isAdding = false;
}

function openOtaDrawer() {
  elements.otaDrawer.classList.add("open");
  elements.otaDrawer.setAttribute("aria-hidden", "false");
  elements.otaBackdrop.classList.add("open");
  elements.otaBackdrop.classList.remove("hidden");
  setInputError(elements.otaFile, false);
}

function closeOtaDrawer() {
  elements.otaDrawer.classList.remove("open");
  elements.otaDrawer.setAttribute("aria-hidden", "true");
  elements.otaBackdrop.classList.remove("open");
  elements.otaBackdrop.classList.add("hidden");
  if (elements.otaForm) {
    elements.otaForm.reset();
  }
  setInputError(elements.otaFile, false);
}

function commitDrawerDevice() {
  if (!state.activeDevice) {
    return false;
  }

  state.activeDevice.enabled = elements.formEnabled.checked;
  state.activeDevice.name = elements.formName.value.trim();
  state.activeDevice.calibration = Number.parseFloat(elements.formCalibration.value);
  state.activeDevice.reversed = elements.formReversed.checked;

  const isValid = validateDevice(state.activeDevice);
  setInputError(elements.formName, !state.activeDevice.name);
  setInputError(elements.formCalibration, !Number.isFinite(state.activeDevice.calibration));
  if (!isValid) {
    return false;
  }

  if (state.isAdding) {
    state.config.devices.push(state.activeDevice);
  }

  renderTable();
  scheduleSave();
  closeDrawer();
  return true;
}

function deleteActiveDevice() {
  if (!state.activeDevice || state.isAdding) {
    return;
  }
  if (!confirm(`Delete device "${state.activeDevice.name}"?`)) {
    return;
  }
  state.config.devices = state.config.devices.filter((device) => device !== state.activeDevice);
  renderTable();
  scheduleSave();
  closeDrawer();
}

function addDevice() {
  if (!state.config) {
    return;
  }
  openDrawer(null);
}

async function loadConfig() {
  try {
    const config = await fetchJson("/config");
    state.config = normalizeConfig(config);
    setSaveStatus("Loaded", "ok");
  } catch (error) {
    console.error(error);
    state.config = normalizeConfig({ devices: [] });
    setSaveStatus("Config load failed", "error");
  }
}

async function loadStatus() {
  if (state.statusInFlight) {
    return;
  }

  state.statusInFlight = true;
  try {
    state.status = await fetchJson("/status");
    updateVersion();
    updateStatusMap();
    updateMetrics();
    setStatusPill(true);
  } catch (error) {
    console.error(error);
    setStatusPill(false);
  } finally {
    state.statusInFlight = false;
  }
}

async function init() {
  setSaveStatus("Loading...", "saving");
  setStatusPill(false);

  await loadConfig();
  renderTable();
  await loadStatus();

  elements.addButton.addEventListener("click", addDevice);
  elements.drawerClose.addEventListener("click", closeDrawer);
  elements.drawerBackdrop.addEventListener("click", closeDrawer);
  elements.formCancel.addEventListener("click", closeDrawer);
  elements.formDelete.addEventListener("click", deleteActiveDevice);
  elements.form.addEventListener("submit", (event) => {
    event.preventDefault();
    commitDrawerDevice();
  });

  if (elements.otaOpen) {
    elements.otaOpen.addEventListener("click", openOtaDrawer);
  }
  if (elements.otaClose) {
    elements.otaClose.addEventListener("click", closeOtaDrawer);
  }
  if (elements.otaCancel) {
    elements.otaCancel.addEventListener("click", closeOtaDrawer);
  }
  if (elements.otaBackdrop) {
    elements.otaBackdrop.addEventListener("click", closeOtaDrawer);
  }
  if (elements.otaFile) {
    elements.otaFile.addEventListener("change", () => {
      setInputError(elements.otaFile, false);
    });
  }
  if (elements.otaForm) {
    elements.otaForm.addEventListener("submit", (event) => {
      if (!elements.otaFile || !elements.otaFile.files || elements.otaFile.files.length === 0) {
        event.preventDefault();
        setInputError(elements.otaFile, true);
      }
    });
  }

  setInterval(loadStatus, 1000);
}

window.addEventListener("DOMContentLoaded", init);
