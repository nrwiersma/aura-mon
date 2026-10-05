import {
  html,
  render,
  useEffect,
  useMemo,
  useRef,
  useState
} from "/preact.js";

const MAX_DEVICES = 15;
const MAX_UPLOADERS = 4;
const SAVE_DEBOUNCE_MS = 600;
const THEME_STORAGE_KEY = "theme";
const DEVICE_ACTION_ENDPOINT = "/device/action";

const UPLOADER_TYPES = [
  { value: "influxdb2", label: "InfluxDB 2.x" },
  { value: "homeassistant", label: "Home Assistant" }
];

// Type-specific settings fields, in display order. Unknown keys present in a
// loaded config (e.g. from a newer firmware) are preserved but not shown.
const UPLOADER_FIELD_DEFS = {
  influxdb2: [
    { key: "url", label: "URL", placeholder: "http://influx.local:8086", required: true },
    { key: "org", label: "Organization", required: true },
    { key: "bucket", label: "Bucket", required: true },
    { key: "token", label: "API token", required: true },
    { key: "measurement", label: "Measurement", placeholder: "aura-mon", required: false }
  ],
  homeassistant: [
    { key: "url", label: "Home Assistant URL", placeholder: "http://homeassistant.local:8123", required: true },
    { key: "webhook_id", label: "Webhook ID", required: true }
  ]
};

function uploaderTypeLabel(type) {
  return UPLOADER_TYPES.find((t) => t.value === type)?.label || type;
}

function defaultUploaderType() {
  return UPLOADER_TYPES[0].value;
}

function setHtmlTheme(theme) {
  const isDark = theme === "dark";
  document.documentElement.classList.toggle("theme-dark", isDark);
}

function getStoredTheme() {
  try {
    const stored = localStorage.getItem(THEME_STORAGE_KEY);
    return stored === "dark" || stored === "light" ? stored : null;
  } catch (error) {
    return null;
  }
}

function getPreferredTheme() {
  const stored = getStoredTheme();
  if (stored) {
    return stored;
  }
  if (window.matchMedia) {
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }
  return "light";
}

async function fetchJson(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`Request failed: ${response.status}`);
  }
  return response.json();
}

function normalizeUploaderSettings(type, settings) {
  const source = settings && typeof settings === "object" ? settings : {};
  const fields = UPLOADER_FIELD_DEFS[type] || [];
  const normalized = {};
  fields.forEach((field) => {
    normalized[field.key] = typeof source[field.key] === "string" ? source[field.key] : "";
  });
  return normalized;
}

function normalizeConfig(config) {
  const normalized = {
    format: Number.isFinite(config.format) ? config.format : 1,
    network: config.network || {},
    devices: Array.isArray(config.devices) ? config.devices : [],
    uploaders: Array.isArray(config.uploaders) ? config.uploaders : []
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

  normalized.uploaders = normalized.uploaders.map((uploader) => {
    const type = UPLOADER_TYPES.some((t) => t.value === uploader.type) ? uploader.type : defaultUploaderType();
    return {
      id: typeof uploader.id === "string" ? uploader.id : "",
      type,
      enabled: typeof uploader.enabled === "boolean" ? uploader.enabled : false,
      interval: Number.isFinite(uploader.interval) ? uploader.interval : 60,
      settings: normalizeUploaderSettings(type, uploader.settings)
    };
  });

  return normalized;
}

function formatMetric(value, decimals = 3) {
  if (!Number.isFinite(value)) {
    return "--";
  }
  const fixed = value.toFixed(decimals);
  return fixed.replace(/\.0+$/, "").replace(/(\.\d*?)0+$/, "$1");
}

function formatBytes(bytes) {
  const value = Number(bytes);
  if (!Number.isFinite(value)) {
    return "--";
  }
  const units = ["B", "KB", "MB", "GB", "TB"];
  let size = value;
  let unitIndex = 0;
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024;
    unitIndex += 1;
  }
  const decimals = size >= 100 ? 0 : size >= 10 ? 1 : 2;
  return `${formatMetric(size, decimals)} ${units[unitIndex]}`;
}

function formatTimestamp(seconds) {
  const value = Number(seconds);
  if (!Number.isFinite(value) || value <= 0) {
    return "--";
  }
  return new Date(value * 1000).toLocaleString();
}

function formatDuration(seconds) {
  const value = Number(seconds);
  if (!Number.isFinite(value) || value < 0) {
    return "--";
  }
  if (value < 60) {
    return `${Math.round(value)}s`;
  }
  if (value < 3600) {
    return `${Math.round(value / 60)}m`;
  }
  return `${Math.round(value / 3600)}h`;
}

// uploaderHealth summarises an uploader's /status entry into a badge
// variant/label and a short detail line, for the Uploaders table.
function uploaderHealth(enabled, uploaderStatus) {
  if (!enabled) {
    return { variant: "muted", label: "Disabled", detail: "Not running" };
  }
  if (!uploaderStatus) {
    return { variant: "pending", label: "Starting", detail: "Waiting for first attempt" };
  }
  if (uploaderStatus.consecutiveFailures > 0) {
    const reason =
      uploaderStatus.lastHttpStatus === -1 ? "connection failed" : `HTTP ${uploaderStatus.lastHttpStatus}`;
    return {
      variant: "error",
      label: "Error",
      detail: `${uploaderStatus.consecutiveFailures} failed in a row (${reason})`
    };
  }
  if (uploaderStatus.successTotal === 0) {
    return { variant: "pending", label: "Pending", detail: `Behind by ${formatDuration(uploaderStatus.lagSeconds)}` };
  }
  return { variant: "ok", label: "OK", detail: `Behind by ${formatDuration(uploaderStatus.lagSeconds)}` };
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

function validateDevice(device) {
  let valid = true;
  const nameValid = device.name && device.name.trim().length > 0;
  const calib = Number(device.calibration);
  const calibValid = Number.isFinite(calib) && calib > 0;
  if (!nameValid) valid = false;
  if (!calibValid) valid = false;
  return valid;
}

function validateConfig(config) {
  let valid = true;
  const addressCounts = new Map();

  config?.devices.forEach((device) => {
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

  config?.devices.forEach((device) => {
    const addr = Number(device.address);
    const duplicate = Number.isInteger(addr) && addressCounts.get(addr) > 1;
    if (duplicate) {
      valid = false;
    }
  });

  if (!validateUploaders(config?.uploaders)) {
    valid = false;
  }

  return valid;
}

function validateUploader(uploader) {
  if (!uploader) return false;
  const idValid = typeof uploader.id === "string" && uploader.id.trim().length > 0;
  const interval = Number(uploader.interval);
  const intervalValid = Number.isInteger(interval) && interval > 0;
  const fields = UPLOADER_FIELD_DEFS[uploader.type] || [];
  const settingsValid = fields.every((field) => {
    if (!field.required) return true;
    const value = uploader.settings?.[field.key];
    return typeof value === "string" && value.trim().length > 0;
  });
  return idValid && intervalValid && settingsValid;
}

function validateUploaders(uploaders) {
  let valid = true;
  const idCounts = new Map();

  (uploaders || []).forEach((uploader) => {
    const id = (uploader.id || "").trim().toLowerCase();
    if (id) {
      idCounts.set(id, (idCounts.get(id) || 0) + 1);
    }
    if (!validateUploader(uploader)) {
      valid = false;
    }
  });

  (uploaders || []).forEach((uploader) => {
    const id = (uploader.id || "").trim().toLowerCase();
    if (id && idCounts.get(id) > 1) {
      valid = false;
    }
  });

  return valid;
}

async function postDeviceAction(action, address) {
  if (!Number.isInteger(address) || address <= 0) {
    console.warn("Device action ignored: invalid address", address);
    return false;
  }

  try {
    const response = await fetch(DEVICE_ACTION_ENDPOINT, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action, address })
    });

    if (!response.ok) {
      throw new Error(`Device action failed: ${response.status}`);
    }

    return true;
  } catch (error) {
    console.error(error);
    return false;
  }
}

async function broadcastDeviceAddress(address) {
  if (!Number.isInteger(address) || address <= 0) {
    console.warn("Broadcast ignored: invalid address", address);
    return;
  }
  await postDeviceAction("assign", address);
}

function findAvailableAddress(devices) {
  if (!devices || devices.length === 0) {
    return 1;
  }
  const maxAddress = Math.max(...devices.map((device) => Number(device.address) || 0));
  const next = maxAddress + 1;
  return next <= MAX_DEVICES ? next : null;
}

function App() {
  const [config, setConfig] = useState(null);
  const [status, setStatus] = useState(null);
  const [isLive, setIsLive] = useState(false);
  const [saveStatus, setSaveStatus] = useState({ text: "Loading...", variant: "saving" });
  const [drawerOpen, setDrawerOpen] = useState(false);
  const [otaOpen, setOtaOpen] = useState(false);
  const [activeDevice, setActiveDevice] = useState(null);
  const [editingIndex, setEditingIndex] = useState(null);
  const [isAdding, setIsAdding] = useState(false);
  const [broadcastChecked, setBroadcastChecked] = useState(false);
  const [fieldErrors, setFieldErrors] = useState({ name: false, calibration: false });
  const [locatingAddresses, setLocatingAddresses] = useState(() => new Set());
  const [otaFileError, setOtaFileError] = useState(false);
  const [otaPublicFileError, setOtaPublicFileError] = useState(false);
  const [theme, setTheme] = useState(getPreferredTheme());
  const [rebootPending, setRebootPending] = useState(false);

  const [uploaderDrawerOpen, setUploaderDrawerOpen] = useState(false);
  const [activeUploader, setActiveUploader] = useState(null);
  const [editingUploaderIndex, setEditingUploaderIndex] = useState(null);
  const [isAddingUploader, setIsAddingUploader] = useState(false);
  const [uploaderFieldErrors, setUploaderFieldErrors] = useState({});

  const saveTimerRef = useRef(null);
  const statusInFlightRef = useRef(false);
  const configRef = useRef(config);
  const otaFormRef = useRef(null);
  const otaPublicFormRef = useRef(null);
  const otaFileRef = useRef(null);
  const otaPublicFileRef = useRef(null);

  useEffect(() => {
    configRef.current = config;
  }, [config]);

  useEffect(() => {
    setHtmlTheme(theme);
  }, [theme]);

  useEffect(() => {
    if (!window.matchMedia) {
      return undefined;
    }
    const media = window.matchMedia("(prefers-color-scheme: dark)");
    if (typeof media.addEventListener !== "function") {
      return undefined;
    }
    const handler = (event) => {
      if (getStoredTheme()) {
        return;
      }
      setTheme(event.matches ? "dark" : "light");
    };
    media.addEventListener("change", handler);
    return () => media.removeEventListener("change", handler);
  }, []);

  function setSaveStatusState(text, variant) {
    setSaveStatus({ text, variant });
  }

  function setThemeAndStore(nextTheme) {
    setHtmlTheme(nextTheme);
    setTheme(nextTheme);
    try {
      localStorage.setItem(THEME_STORAGE_KEY, nextTheme);
    } catch (error) {
      // Ignore storage errors and still apply the theme.
    }
  }

  function toggleTheme() {
    const nextTheme = theme === "dark" ? "light" : "dark";
    setThemeAndStore(nextTheme);
  }

  async function loadConfig() {
    try {
      const loaded = await fetchJson("/config");
      setConfig(normalizeConfig(loaded));
      setSaveStatusState("Loaded", "ok");
    } catch (error) {
      console.error(error);
      setConfig(normalizeConfig({ devices: [] }));
      setSaveStatusState("Config load failed", "error");
    }
  }

  async function loadStatus() {
    if (statusInFlightRef.current) {
      return;
    }

    statusInFlightRef.current = true;
    try {
      const nextStatus = await fetchJson("/status");
      setStatus(nextStatus);
      setIsLive(true);
    } catch (error) {
      console.error(error);
      setIsLive(false);
    } finally {
      statusInFlightRef.current = false;
    }
  }

  async function saveConfig() {
    const current = configRef.current;
    if (!current) {
      return;
    }

    if (!validateConfig(current)) {
      setSaveStatusState("Fix highlighted fields", "error");
      return;
    }

    setSaveStatusState("Saving...", "saving");

    try {
      const response = await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(current)
      });

      if (!response.ok) {
        throw new Error(`Save failed: ${response.status}`);
      }

      setSaveStatusState("Saved", "ok");
    } catch (error) {
      console.error(error);
      setSaveStatusState("Save failed", "error");
    }
  }

  function scheduleSave() {
    const current = configRef.current;
    if (!current) {
      return;
    }

    if (!validateConfig(current)) {
      setSaveStatusState("Fix highlighted fields", "error");
      return;
    }

    setSaveStatusState("Unsaved changes", "saving");
    clearTimeout(saveTimerRef.current);
    saveTimerRef.current = setTimeout(saveConfig, SAVE_DEBOUNCE_MS);
  }

  useEffect(() => {
    setSaveStatusState("Loading...", "saving");
    setIsLive(false);

    loadConfig();
    loadStatus();

    const interval = setInterval(loadStatus, 1000);
    return () => {
      clearInterval(interval);
      clearTimeout(saveTimerRef.current);
    };
  }, []);

  const statusMap = useMemo(() => {
    const map = new Map();
    (status?.devices || []).forEach((device) => {
      if (device.name) {
        map.set(device.name, device);
      }
    });
    return map;
  }, [status]);

  const uploaderStatusMap = useMemo(() => {
    const map = new Map();
    (status?.uploaders || []).forEach((uploader) => {
      if (uploader.id) {
        map.set(uploader.id, uploader);
      }
    });
    return map;
  }, [status]);

  const devices = config?.devices || [];
  const deviceCount = devices.length;
  const uploaders = config?.uploaders || [];
  const uploaderCount = uploaders.length;
  const versionText = status?.version || "--";
  const themePressed = theme === "dark" ? "true" : "false";
  const themeLabel = theme === "dark" ? "Switch to light mode" : "Switch to dark mode";
  const drawerTitle = isAdding ? "Add device" : "Edit device";
  const uploaderDrawerTitle = isAddingUploader ? "Add uploader" : "Edit uploader";
  const datalog = status?.datalog || {};
  const network = status?.network || {};
  const activeUploaderFields = UPLOADER_FIELD_DEFS[activeUploader?.type] || [];

  function closeDrawer() {
    setDrawerOpen(false);
    setActiveDevice(null);
    setEditingIndex(null);
    setIsAdding(false);
    setBroadcastChecked(false);
    setFieldErrors({ name: false, calibration: false });
  }

  function openDrawerFor(device, index) {
    if (!configRef.current) {
      return;
    }

    if (!device) {
      const address = findAvailableAddress(configRef.current.devices);
      if (!address) {
        return;
      }
      setActiveDevice({
        enabled: true,
        address,
        name: "",
        calibration: "1.0",
        reversed: false
      });
      setIsAdding(true);
      setEditingIndex(null);
      setBroadcastChecked(true);
    } else {
      setActiveDevice({
        enabled: Boolean(device.enabled),
        address: device.address,
        name: device.name || "",
        calibration: Number.isFinite(device.calibration) ? String(device.calibration) : "",
        reversed: Boolean(device.reversed)
      });
      setIsAdding(false);
      setEditingIndex(index);
      setBroadcastChecked(false);
    }

    setFieldErrors({ name: false, calibration: false });
    setDrawerOpen(true);
  }

  function handleActiveChange(field) {
    return (event) => {
      const { type, checked, value } = event.target;
      const nextValue = type === "checkbox" ? checked : value;
      setActiveDevice((prev) => (prev ? { ...prev, [field]: nextValue } : prev));
      if (field === "name") {
        setFieldErrors((prev) => ({ ...prev, name: false }));
      }
      if (field === "calibration") {
        setFieldErrors((prev) => ({ ...prev, calibration: false }));
      }
    };
  }

  function commitDrawerDevice(event) {
    if (event) {
      event.preventDefault();
    }
    if (!activeDevice) {
      return false;
    }

    const trimmedName = activeDevice.name.trim();
    const calibrationValue = Number.parseFloat(activeDevice.calibration);
    const nextDevice = {
      enabled: Boolean(activeDevice.enabled),
      address: Number(activeDevice.address),
      name: trimmedName,
      calibration: calibrationValue,
      reversed: Boolean(activeDevice.reversed)
    };

    const isValid = validateDevice(nextDevice);
    setFieldErrors({
      name: !trimmedName,
      calibration: !Number.isFinite(calibrationValue)
    });

    if (!isValid) {
      return false;
    }

    setConfig((prev) => {
      if (!prev) {
        return prev;
      }
      const nextDevices = [...prev.devices];
      if (isAdding) {
        nextDevices.push(nextDevice);
      } else if (Number.isInteger(editingIndex) && editingIndex >= 0) {
        nextDevices[editingIndex] = nextDevice;
      }
      return { ...prev, devices: nextDevices };
    });

    scheduleSave();
    closeDrawer();

    if (broadcastChecked) {
      broadcastDeviceAddress(nextDevice.address);
    }

    return true;
  }

  function deleteActiveDevice() {
    if (!activeDevice || isAdding) {
      return;
    }
    if (!confirm(`Delete device "${activeDevice.name}"?`)) {
      return;
    }

    setConfig((prev) => {
      if (!prev) {
        return prev;
      }
      const nextDevices = prev.devices.filter((_, idx) => idx !== editingIndex);
      return { ...prev, devices: nextDevices };
    });

    scheduleSave();
    closeDrawer();
  }

  async function locateDevice(device) {
    if (!device) {
      return;
    }
    const address = Number(device.address);
    if (!Number.isInteger(address) || address <= 0) {
      console.warn("Locate ignored: invalid address", address);
      return;
    }
    setLocatingAddresses((prev) => {
      const next = new Set(prev);
      next.add(address);
      return next;
    });
    await postDeviceAction("locate", address);
    setLocatingAddresses((prev) => {
      const next = new Set(prev);
      next.delete(address);
      return next;
    });
  }

  function closeUploaderDrawer() {
    setUploaderDrawerOpen(false);
    setActiveUploader(null);
    setEditingUploaderIndex(null);
    setIsAddingUploader(false);
    setUploaderFieldErrors({});
  }

  function openUploaderDrawerFor(uploader, index) {
    if (!configRef.current) {
      return;
    }

    if (!uploader) {
      const type = defaultUploaderType();
      setActiveUploader({
        enabled: true,
        id: "",
        type,
        interval: "60",
        settings: normalizeUploaderSettings(type, {})
      });
      setIsAddingUploader(true);
      setEditingUploaderIndex(null);
    } else {
      setActiveUploader({
        enabled: Boolean(uploader.enabled),
        id: uploader.id || "",
        type: uploader.type,
        interval: Number.isFinite(uploader.interval) ? String(uploader.interval) : "",
        settings: { ...uploader.settings }
      });
      setIsAddingUploader(false);
      setEditingUploaderIndex(index);
    }

    setUploaderFieldErrors({});
    setUploaderDrawerOpen(true);
  }

  function handleUploaderChange(field) {
    return (event) => {
      const { type, checked, value } = event.target;
      const nextValue = type === "checkbox" ? checked : value;
      setActiveUploader((prev) => (prev ? { ...prev, [field]: nextValue } : prev));
      setUploaderFieldErrors((prev) => ({ ...prev, [field]: false }));
    };
  }

  function handleUploaderTypeChange(event) {
    const nextType = event.target.value;
    setActiveUploader((prev) =>
      prev ? { ...prev, type: nextType, settings: normalizeUploaderSettings(nextType, prev.settings) } : prev
    );
    setUploaderFieldErrors({});
  }

  function handleUploaderSettingChange(key) {
    return (event) => {
      const value = event.target.value;
      setActiveUploader((prev) =>
        prev ? { ...prev, settings: { ...prev.settings, [key]: value } } : prev
      );
      setUploaderFieldErrors((prev) => ({ ...prev, [key]: false }));
    };
  }

  function commitUploaderDrawer(event) {
    if (event) {
      event.preventDefault();
    }
    if (!activeUploader) {
      return false;
    }

    const trimmedId = activeUploader.id.trim();
    const interval = Number.parseInt(activeUploader.interval, 10);
    const nextUploader = {
      enabled: Boolean(activeUploader.enabled),
      id: trimmedId,
      type: activeUploader.type,
      interval,
      settings: { ...activeUploader.settings }
    };

    const isValid = validateUploader(nextUploader);
    const fields = UPLOADER_FIELD_DEFS[nextUploader.type] || [];
    const errors = {
      id: !trimmedId,
      interval: !Number.isInteger(interval) || interval <= 0
    };
    fields.forEach((field) => {
      if (!field.required) return;
      const value = nextUploader.settings[field.key];
      errors[field.key] = !(typeof value === "string" && value.trim().length > 0);
    });
    setUploaderFieldErrors(errors);

    if (!isValid) {
      return false;
    }

    // An id must be unique among the other uploaders (not counting the one being edited).
    const current = configRef.current;
    const duplicate = (current?.uploaders || []).some(
      (u, idx) => idx !== editingUploaderIndex && u.id.trim().toLowerCase() === trimmedId.toLowerCase()
    );
    if (duplicate) {
      setUploaderFieldErrors((prev) => ({ ...prev, id: true }));
      return false;
    }

    setConfig((prev) => {
      if (!prev) {
        return prev;
      }
      const nextUploaders = [...prev.uploaders];
      if (isAddingUploader) {
        nextUploaders.push(nextUploader);
      } else if (Number.isInteger(editingUploaderIndex) && editingUploaderIndex >= 0) {
        nextUploaders[editingUploaderIndex] = nextUploader;
      }
      return { ...prev, uploaders: nextUploaders };
    });

    scheduleSave();
    closeUploaderDrawer();

    return true;
  }

  function deleteActiveUploader() {
    if (!activeUploader || isAddingUploader) {
      return;
    }
    if (!confirm(`Delete uploader "${activeUploader.id}"?`)) {
      return;
    }

    setConfig((prev) => {
      if (!prev) {
        return prev;
      }
      const nextUploaders = prev.uploaders.filter((_, idx) => idx !== editingUploaderIndex);
      return { ...prev, uploaders: nextUploaders };
    });

    scheduleSave();
    closeUploaderDrawer();
  }

  function openOtaDrawer() {
    setOtaOpen(true);
    setOtaFileError(false);
    setOtaPublicFileError(false);
  }

  function closeOtaDrawer() {
    setOtaOpen(false);
    setOtaFileError(false);
    setOtaPublicFileError(false);
    if (otaFormRef.current) {
      otaFormRef.current.reset();
    }
    if (otaPublicFormRef.current) {
      otaPublicFormRef.current.reset();
    }
  }

  function handleOtaSubmit(event) {
    const input = otaFileRef.current;
    if (!input || !input.files || input.files.length === 0) {
      event.preventDefault();
      setOtaFileError(true);
    }
  }

  function handleOtaPublicSubmit(event) {
    const input = otaPublicFileRef.current;
    if (!input || !input.files || input.files.length === 0) {
      event.preventDefault();
      setOtaPublicFileError(true);
    }
  }

  async function rebootDevice() {
    if (rebootPending) {
      return;
    }
    if (!confirm("Reboot device now?")) {
      return;
    }

    setRebootPending(true);

    try {
      const response = await fetch("/reboot", { method: "POST" });
      if (!response.ok) {
        throw new Error(`Reboot failed: ${response.status}`);
      }
    } catch (error) {
      console.error(error);
      alert("Reboot failed. Try again.");
      setRebootPending(false);
    }
  }

  return html`
    <div>
      <div class="page">
        <header class="topbar">
          <img class="logo" src="/logo.svg" alt="Aura Mon" />
          <div class="topbar-meta">
            <div class="version-wrap">
              <button
                id="theme-toggle"
                class="btn btn-ghost btn-theme"
                type="button"
                aria-label=${themeLabel}
                aria-pressed=${themePressed}
                onClick=${toggleTheme}
              >
                <span class="theme-icon theme-icon-moon" aria-hidden="true"></span>
                <span class="theme-icon theme-icon-sun" aria-hidden="true"></span>
              </button>
              <div class="version">
                Version <span id="version">${versionText}</span>
              </div>
              <button
                id="reboot-device"
                class="btn btn-ghost"
                type="button"
                aria-label="Reboot device"
                title="Reboot device"
                disabled=${rebootPending}
                onClick=${rebootDevice}
              >
                <span class="icon icon-reboot" aria-hidden="true"></span>
              </button>
              <button
                id="ota-open"
                class="btn btn-ghost btn-ota"
                type="button"
                aria-haspopup="dialog"
                aria-controls="ota-drawer"
                aria-label="Open OTA upload"
                onClick=${openOtaDrawer}
              >
                <span class="icon icon-ota" aria-hidden="true"></span>
              </button>
            </div>
            <div id="status-pill" class=${`status-pill${isLive ? "" : " offline"}`}>
              ${isLive ? "Live" : "Offline"}
            </div>
          </div>
        </header>

        <main class="content">
          <section class="card">
            <div class="card-header">
              <div>
                <h1>
                  Devices <span id="device-count" class="device-count">${deviceCount}/${MAX_DEVICES}</span>
                </h1>
              </div>
              <div class="actions">
                <span id="save-status" class=${`save-status ${saveStatus.variant || ""}`}>
                  ${saveStatus.text}
                </span>
              </div>
            </div>

            <div class="table-shell">
              <div class="table-wrap">
                <table class="device-table" aria-label="Device configuration">
                  <thead>
                    <tr>
                      <th></th>
                      <th>Name</th>
                      <th>Volts</th>
                      <th>Power</th>
                      <th>Actions</th>
                    </tr>
                  </thead>
                  <tbody id="devices-body">
                    ${devices.map((device, index) => {
                      const name = device?.name ? device.name.trim() : "";
                      const metrics = name ? statusMap.get(name) : null;
                      const locateDisabled = locatingAddresses.has(Number(device.address));
                      return html`
                        <tr class=${device.enabled ? "" : "row-disabled"}>
                          <td class="fit-content">
                            <span class="address-chip">${Number.isFinite(device.address) ? String(device.address) : "--"}</span>
                          </td>
                          <td>${device.name || "--"}</td>
                          <td data-volts-text="true">${formatVoltage(metrics)}</td>
                          <td class="metric" data-power="true">
                            <span class="power-cell">
                              <span data-power-text="true">${formatPower(metrics)}</span>
                              ${device.reversed
                                ? html`<span class="power-icon active" title="Reversed">
                                    <span class="icon icon-reversed" aria-hidden="true"></span>
                                  </span>`
                                : ""}
                            </span>
                          </td>
                          <td class="fit-content">
                            <div class="row-actions">
                              <button
                                type="button"
                                class="btn-icon-only"
                                aria-label="Locate device"
                                disabled=${locateDisabled}
                                onClick=${() => locateDevice(device)}
                              >
                                <span class="icon icon-locate" aria-hidden="true"></span>
                              </button>
                              <button
                                type="button"
                                class="btn-icon-only"
                                aria-label="Edit device"
                                onClick=${() => openDrawerFor(device, index)}
                              >
                                <span class="icon icon-edit" aria-hidden="true"></span>
                              </button>
                            </div>
                          </td>
                        </tr>
                      `;
                    })}
                  </tbody>
                </table>
              </div>

              <div id="empty-state" class=${deviceCount === 0 ? "empty-state" : "empty-state hidden"}>
                No devices configured yet. Add a device to get started.
              </div>

              <div class="table-actions">
                <button
                  id="add-device"
                  class="btn btn-primary"
                  type="button"
                  disabled=${deviceCount >= MAX_DEVICES}
                  onClick=${() => openDrawerFor(null, null)}
                >
                  <span class="btn-icon">+</span>
                  Add device
                </button>
              </div>
            </div>

            <div class="info-section" aria-label="Information">
              <h2 class="section-title">Information</h2>
              <div class="info-grid">
                <div class="info-card">
                  <h3 class="info-card-title">Data log</h3>
                  <dl class="info-list">
                    <div class="info-row">
                      <dt>First timestamp</dt>
                      <dd>${formatTimestamp(datalog.firstTS)}</dd>
                    </div>
                    <div class="info-row">
                      <dt>Last timestamp</dt>
                      <dd>${formatTimestamp(datalog.lastTS)}</dd>
                    </div>
                    <div class="info-row">
                      <dt>Size</dt>
                      <dd>${formatBytes(datalog.size)}</dd>
                    </div>
                  </dl>
                </div>

                <div class="info-card">
                  <h3 class="info-card-title">Network</h3>
                  <dl class="info-list">
                    <div class="info-row">
                      <dt>Hostname</dt>
                      <dd>${network.hostname || "--"}</dd>
                    </div>
                    <div class="info-row">
                      <dt>IP address</dt>
                      <dd>${network.ip || "--"}</dd>
                    </div>
                    <div class="info-row">
                      <dt>Gateway</dt>
                      <dd>${network.gateway || "--"}</dd>
                    </div>
                    <div class="info-row">
                      <dt>Subnet</dt>
                      <dd>${network.subnet || "--"}</dd>
                    </div>
                    <div class="info-row">
                      <dt>DNS</dt>
                      <dd>${network.dns || "--"}</dd>
                    </div>
                    <div class="info-row">
                      <dt>MAC</dt>
                      <dd>${network.mac || "--"}</dd>
                    </div>
                  </dl>
                </div>
              </div>
            </div>
          </section>

          <section class="card uploaders-card">
            <div class="card-header">
              <div>
                <h1>
                  Uploaders <span id="uploader-count" class="device-count">${uploaderCount}/${MAX_UPLOADERS}</span>
                </h1>
              </div>
            </div>

            <div class="table-shell">
              <div class="table-wrap">
                <table class="device-table" aria-label="Uploader configuration">
                  <thead>
                    <tr>
                      <th>ID</th>
                      <th>Type</th>
                      <th>Interval</th>
                      <th>Status</th>
                      <th>Actions</th>
                    </tr>
                  </thead>
                  <tbody id="uploaders-body">
                    ${uploaders.map((uploader, index) => {
                      const uploaderStatus = uploader.id ? uploaderStatusMap.get(uploader.id) : null;
                      const health = uploaderHealth(uploader.enabled, uploaderStatus);
                      return html`
                        <tr class=${uploader.enabled ? "" : "row-disabled"}>
                          <td>${uploader.id || "--"}</td>
                          <td>${uploaderTypeLabel(uploader.type)}</td>
                          <td>${Number.isFinite(uploader.interval) ? `${uploader.interval}s` : "--"}</td>
                          <td>
                            <span class=${`badge badge-${health.variant}`}>${health.label}</span>
                            <span class="uploader-detail">${health.detail}</span>
                          </td>
                          <td class="fit-content">
                            <div class="row-actions">
                              <button
                                type="button"
                                class="btn-icon-only"
                                aria-label="Edit uploader"
                                onClick=${() => openUploaderDrawerFor(uploader, index)}
                              >
                                <span class="icon icon-edit" aria-hidden="true"></span>
                              </button>
                            </div>
                          </td>
                        </tr>
                      `;
                    })}
                  </tbody>
                </table>
              </div>

              <div id="uploaders-empty-state" class=${uploaderCount === 0 ? "empty-state" : "empty-state hidden"}>
                No uploaders configured yet. Add one to push data to InfluxDB2 or Home Assistant.
              </div>

              <div class="table-actions">
                <button
                  id="add-uploader"
                  class="btn btn-primary"
                  type="button"
                  disabled=${uploaderCount >= MAX_UPLOADERS}
                  onClick=${() => openUploaderDrawerFor(null, null)}
                >
                  <span class="btn-icon">+</span>
                  Add uploader
                </button>
              </div>
            </div>
          </section>
        </main>
      </div>

      <div
        id="drawer-backdrop"
        class=${drawerOpen ? "drawer-backdrop open" : "drawer-backdrop hidden"}
        onClick=${closeDrawer}
      ></div>
      <aside id="device-drawer" class=${drawerOpen ? "drawer open" : "drawer"} aria-hidden=${drawerOpen ? "false" : "true"}>
        <div class="drawer-header">
          <div>
            <div id="drawer-title" class="drawer-title">${drawerTitle}</div>
            <div class="drawer-subtitle">Update device configuration and save.</div>
          </div>
          <button id="drawer-close" class="btn btn-ghost" type="button" aria-label="Close" onClick=${closeDrawer}>
            ×
          </button>
        </div>

        <form id="device-form" class="drawer-body" onSubmit=${commitDrawerDevice}>
          <label class="field">
            <span class="field-label">Enabled</span>
            <span class="toggle">
              <input
                id="form-enabled"
                type="checkbox"
                checked=${Boolean(activeDevice?.enabled)}
                onChange=${handleActiveChange("enabled")}
              />
              <span class="toggle-track" aria-hidden="true"></span>
            </span>
          </label>

          <label class="field">
            <span class="field-label">Address</span>
            <input id="form-address" type="text" readOnly value=${activeDevice?.address ?? ""} />
          </label>

          <label class="field">
            <span class="field-label">Name</span>
            <input
              id="form-name"
              type="text"
              placeholder="Device name"
              required
              class=${fieldErrors.name ? "input-error" : ""}
              value=${activeDevice?.name ?? ""}
              onInput=${handleActiveChange("name")}
            />
          </label>

          <label class="field">
            <span class="field-label">Calibration</span>
            <input
              id="form-calibration"
              type="number"
              min="0.01"
              max="2"
              step="0.001"
              required
              class=${fieldErrors.calibration ? "input-error" : ""}
              value=${activeDevice?.calibration ?? ""}
              onInput=${handleActiveChange("calibration")}
            />
          </label>

          <label class="field">
            <span class="field-label">Reversed</span>
            <span class="toggle">
              <input
                id="form-reversed"
                type="checkbox"
                checked=${Boolean(activeDevice?.reversed)}
                onChange=${handleActiveChange("reversed")}
              />
              <span class="toggle-track" aria-hidden="true"></span>
            </span>
          </label>

          <label class="field">
            <span class="field-label">Broadcast address</span>
            <span class="toggle">
              <input
                id="form-broadcast"
                type="checkbox"
                checked=${broadcastChecked}
                onChange=${(event) => setBroadcastChecked(event.target.checked)}
              />
              <span class="toggle-track" aria-hidden="true"></span>
            </span>
          </label>

          <div class="drawer-actions">
            <button
              id="form-delete"
              class="btn btn-danger"
              type="button"
              disabled=${isAdding}
              style=${isAdding ? "visibility: hidden;" : "visibility: visible;"}
              onClick=${deleteActiveDevice}
            >
              Delete
            </button>
            <div class="drawer-actions-right">
              <button id="form-cancel" class="btn" type="button" onClick=${closeDrawer}>Cancel</button>
              <button id="form-save" class="btn btn-primary" type="submit">Save</button>
            </div>
          </div>
        </form>
      </aside>

      <div
        id="uploader-drawer-backdrop"
        class=${uploaderDrawerOpen ? "drawer-backdrop open" : "drawer-backdrop hidden"}
        onClick=${closeUploaderDrawer}
      ></div>
      <aside
        id="uploader-drawer"
        class=${uploaderDrawerOpen ? "drawer open" : "drawer"}
        aria-hidden=${uploaderDrawerOpen ? "false" : "true"}
      >
        <div class="drawer-header">
          <div>
            <div id="uploader-drawer-title" class="drawer-title">${uploaderDrawerTitle}</div>
            <div class="drawer-subtitle">Update uploader configuration and save.</div>
          </div>
          <button
            id="uploader-drawer-close"
            class="btn btn-ghost"
            type="button"
            aria-label="Close"
            onClick=${closeUploaderDrawer}
          >
            ×
          </button>
        </div>

        <form id="uploader-form" class="drawer-body" onSubmit=${commitUploaderDrawer}>
          <label class="field">
            <span class="field-label">Enabled</span>
            <span class="toggle">
              <input
                id="uploader-form-enabled"
                type="checkbox"
                checked=${Boolean(activeUploader?.enabled)}
                onChange=${handleUploaderChange("enabled")}
              />
              <span class="toggle-track" aria-hidden="true"></span>
            </span>
          </label>

          <label class="field">
            <span class="field-label">ID</span>
            <input
              id="uploader-form-id"
              type="text"
              placeholder="e.g. influx-main"
              required
              readOnly=${!isAddingUploader}
              class=${uploaderFieldErrors.id ? "input-error" : ""}
              value=${activeUploader?.id ?? ""}
              onInput=${handleUploaderChange("id")}
            />
          </label>

          <label class="field">
            <span class="field-label">Type</span>
            <select
              id="uploader-form-type"
              value=${activeUploader?.type ?? ""}
              onChange=${handleUploaderTypeChange}
            >
              ${UPLOADER_TYPES.map(
                (type) => html`<option value=${type.value}>${type.label}</option>`
              )}
            </select>
          </label>

          <label class="field">
            <span class="field-label">Interval (seconds)</span>
            <input
              id="uploader-form-interval"
              type="number"
              min="1"
              step="1"
              required
              class=${uploaderFieldErrors.interval ? "input-error" : ""}
              value=${activeUploader?.interval ?? ""}
              onInput=${handleUploaderChange("interval")}
            />
          </label>

          ${activeUploaderFields.map(
            (field) => html`
              <label class="field">
                <span class="field-label">${field.label}</span>
                <input
                  type="text"
                  placeholder=${field.placeholder || ""}
                  required=${field.required}
                  class=${uploaderFieldErrors[field.key] ? "input-error" : ""}
                  value=${activeUploader?.settings?.[field.key] ?? ""}
                  onInput=${handleUploaderSettingChange(field.key)}
                />
              </label>
            `
          )}

          <div class="drawer-actions">
            <button
              id="uploader-form-delete"
              class="btn btn-danger"
              type="button"
              disabled=${isAddingUploader}
              style=${isAddingUploader ? "visibility: hidden;" : "visibility: visible;"}
              onClick=${deleteActiveUploader}
            >
              Delete
            </button>
            <div class="drawer-actions-right">
              <button id="uploader-form-cancel" class="btn" type="button" onClick=${closeUploaderDrawer}>
                Cancel
              </button>
              <button id="uploader-form-save" class="btn btn-primary" type="submit">Save</button>
            </div>
          </div>
        </form>
      </aside>

      <div
        id="ota-backdrop"
        class=${otaOpen ? "drawer-backdrop open" : "drawer-backdrop hidden"}
        onClick=${closeOtaDrawer}
      ></div>
      <aside id="ota-drawer" class=${otaOpen ? "drawer open" : "drawer"} aria-hidden=${otaOpen ? "false" : "true"}>
        <div class="drawer-header">
          <div>
            <div class="drawer-title">Firmware update</div>
            <div class="drawer-subtitle">Upload a new firmware file to update the device.</div>
          </div>
          <button id="ota-close" class="btn btn-ghost" type="button" aria-label="Close" onClick=${closeOtaDrawer}>
            ×
          </button>
        </div>

        <div class="drawer-body">
          <form id="ota-form" action="/ota" method="POST" enctype="multipart/form-data" ref=${otaFormRef} onSubmit=${handleOtaSubmit}>
            <label class="field" for="firmware">
              <span class="field-label">Firmware file</span>
              <input
                id="firmware"
                name="firmware"
                type="file"
                class=${`file-input${otaFileError ? " input-error" : ""}`}
                ref=${otaFileRef}
                onChange=${() => setOtaFileError(false)}
              />
            </label>

            <p class="helper-text">Select a firmware file and submit to start the OTA update.</p>

            <div class="drawer-actions">
              <div class="drawer-actions-right">
                <button class="btn btn-primary" type="submit">Upload firmware</button>
              </div>
            </div>
          </form>

          <form
            id="ota-public-form"
            action="/ota/public"
            method="POST"
            enctype="multipart/form-data"
            ref=${otaPublicFormRef}
            onSubmit=${handleOtaPublicSubmit}
          >
            <label class="field" for="public-file">
              <span class="field-label">Public asset</span>
              <input
                id="public-file"
                name="file"
                type="file"
                class=${`file-input${otaPublicFileError ? " input-error" : ""}`}
                ref=${otaPublicFileRef}
                onChange=${() => setOtaPublicFileError(false)}
              />
            </label>

            <p class="helper-text">Upload a file to the SD card public folder for the web UI.</p>

            <div class="drawer-actions">
              <div class="drawer-actions-right">
                <button class="btn btn-primary" type="submit">Upload public file</button>
              </div>
            </div>
          </form>

          <div class="drawer-actions">
            <button class="btn" type="button" id="ota-cancel" onClick=${closeOtaDrawer}>Cancel</button>
          </div>
        </div>
      </aside>
    </div>
  `;
}

const root = document.getElementById("app-root");
if (root) {
  render(html`<${App} />`, root);
}
