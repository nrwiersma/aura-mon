import {
  html,
  render,
  useEffect,
  useMemo,
  useRef,
  useState
} from "/preact.js";

const MAX_DEVICES = 15;
const SAVE_DEBOUNCE_MS = 600;
const THEME_STORAGE_KEY = "theme";
const DEVICE_ACTION_ENDPOINT = "/device/action";

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

  const devices = config?.devices || [];
  const deviceCount = devices.length;
  const versionText = status?.version || "--";
  const themePressed = theme === "dark" ? "true" : "false";
  const themeLabel = theme === "dark" ? "Switch to light mode" : "Switch to dark mode";
  const drawerTitle = isAdding ? "Add device" : "Edit device";

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
                <svg
                  class="theme-icon theme-icon-moon"
                  width="16"
                  height="16"
                  viewBox="0 0 24 24"
                  fill="none"
                  xmlns="http://www.w3.org/2000/svg"
                  aria-hidden="true"
                >
                  <path
                    d="M21 12.79A9 9 0 1 1 11.21 3a7 7 0 1 0 9.79 9.79z"
                    stroke="currentColor"
                    stroke-width="1.6"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  />
                </svg>
                <svg
                  class="theme-icon theme-icon-sun"
                  width="16"
                  height="16"
                  viewBox="0 0 24 24"
                  fill="none"
                  xmlns="http://www.w3.org/2000/svg"
                  aria-hidden="true"
                >
                  <circle cx="12" cy="12" r="4" stroke="currentColor" stroke-width="1.6" />
                  <path
                    d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"
                    stroke="currentColor"
                    stroke-width="1.6"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  />
                </svg>
              </button>
              <div class="version">
                Version <span id="version">${versionText}</span>
              </div>
              <button
                id="ota-open"
                class="btn btn-ghost btn-ota"
                type="button"
                aria-haspopup="dialog"
                aria-controls="ota-drawer"
                aria-label="Open OTA upload"
                onClick=${openOtaDrawer}
              >
                <svg
                  class="ota-icon"
                  width="16"
                  height="16"
                  viewBox="0 0 24 24"
                  fill="none"
                  xmlns="http://www.w3.org/2000/svg"
                  aria-hidden="true"
                >
                  <path d="M12 16V4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" />
                  <path
                    d="M8 8l4-4 4 4"
                    stroke="currentColor"
                    stroke-width="1.6"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  />
                  <path d="M4 20h16" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" />
                </svg>
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
                                    <svg
                                      width="16"
                                      height="16"
                                      viewBox="0 0 24 24"
                                      fill="none"
                                      xmlns="http://www.w3.org/2000/svg"
                                      aria-hidden="true"
                                    >
                                      <path
                                        d="M12 5a7 7 0 1 1-6.22 10.22"
                                        stroke="currentColor"
                                        stroke-width="1.6"
                                        stroke-linecap="round"
                                        stroke-linejoin="round"
                                      />
                                      <path
                                        d="M4.5 9V5.5H8"
                                        stroke="currentColor"
                                        stroke-width="1.6"
                                        stroke-linecap="round"
                                        stroke-linejoin="round"
                                      />
                                    </svg>
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
                                <svg
                                  width="16"
                                  height="16"
                                  viewBox="0 0 24 24"
                                  fill="none"
                                  xmlns="http://www.w3.org/2000/svg"
                                  aria-hidden="true"
                                >
                                  <circle cx="12" cy="12" r="7" stroke="currentColor" stroke-width="1.6" />
                                  <circle cx="12" cy="12" r="2" stroke="currentColor" stroke-width="1.6" />
                                  <path
                                    d="M12 3v2M12 19v2M3 12h2M19 12h2"
                                    stroke="currentColor"
                                    stroke-width="1.6"
                                    stroke-linecap="round"
                                  />
                                </svg>
                              </button>
                              <button
                                type="button"
                                class="btn-icon-only"
                                aria-label="Edit device"
                                onClick=${() => openDrawerFor(device, index)}
                              >
                                <svg
                                  width="16"
                                  height="16"
                                  viewBox="0 0 24 24"
                                  fill="none"
                                  xmlns="http://www.w3.org/2000/svg"
                                >
                                  <path
                                    d="M4 20h4l10-10a2.828 2.828 0 1 0-4-4L4 16v4z"
                                    stroke="currentColor"
                                    stroke-width="1.6"
                                    stroke-linecap="round"
                                    stroke-linejoin="round"
                                  />
                                  <path
                                    d="M13 7l4 4"
                                    stroke="#344054"
                                    stroke-width="1.6"
                                    stroke-linecap="round"
                                    stroke-linejoin="round"
                                  />
                                </svg>
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
            <span class="btn-icon">×</span>
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
            <span class="btn-icon">×</span>
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
