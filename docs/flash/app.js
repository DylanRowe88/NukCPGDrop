/* global JSZip */
import { ESPLoader, Transport } from 'esptool-js';

const GITHUB_OWNER = 'DylanRowe88';
const GITHUB_REPO = 'NukCPGDrop';
const FIRMWARE_BUNDLE_PREFIX = 'NukCPGDrop-Display-';

const FIRMWARE_OFFSETS = {
  'bootloader.bin': 0x0,
  'partition-table.bin': 0x8000,
  'NukCPGDrop.bin': 0x10000,
  'ota_data_initial.bin': 0xD000,
};

const FIRMWARE_FILES = ['bootloader.bin', 'partition-table.bin', 'NukCPGDrop.bin', 'ota_data_initial.bin'];

const FLASH_CONFIG = {
  flashMode: 'dio',
  flashFreq: '80m',
  flashSize: '16MB',
};

let state = {
  step: 1,
  loader: null,
  transport: null,
  port: null,
  chipName: '',
  chipDesc: '',
  chipRevision: 0,
  mac: '',
  flashSize: '',
  releases: [],
  selectedRelease: null,
  firmwareFiles: null,
  installedRelease: null,
  flashStartTime: 0,
  verifyStartTime: 0,
  prevPorts: [],
};

const steps = document.querySelectorAll('.step');
const els = {};

function el(id) { return document.getElementById(id); }

function cacheElements() {
  const ids = [
    'step1', 'step2', 'step3', 'step4',
    'btn-connect', 'btn-connect-prev', 'btn-change-port',
    'release-table-body',
    'btn-flash',
    'progress-area',
    'verify-progress-fill', 'verify-progress-label',
    'result-icon', 'result-version', 'result-summary',
    'result-stats', 'result-btn-dashboard', 'result-btn-again',
    'status-msg', 'error-details',
    'release-count', 'btn-refresh-releases',
    'chip-name', 'chip-desc', 'chip-revision', 'chip-mac',
    'chip-flash-size', 'current-fw-version',
    'selected-version-info',
    'flash-total-progress2', 'flash-total-label2',
    'chip-info-card',
  ];
  ids.forEach(id => { els[id] = el(id); });
}

function showStep(n) {
  steps.forEach((s, i) => s.classList.toggle('active', i + 1 === n));
  state.step = n;
}

function status(msg, type) {
  const s = els['status-msg'];
  s.textContent = msg;
  s.className = 'status ' + type;
  s.classList.remove('hidden');
}

function clearStatus() {
  els['status-msg'].classList.add('hidden');
}

function showError(msg, detail) {
  status(msg, 'error');
  if (detail) {
    els['error-details'].textContent = detail;
    els['error-details'].classList.remove('hidden');
  }
}

function clearError() {
  els['error-details'].classList.add('hidden');
}

function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(1) + ' MB';
}

function formatDate(iso) {
  return iso ? iso.slice(0, 10) : '';
}

function formatRate(bytesPerSec) {
  if (bytesPerSec < 1024) return bytesPerSec.toFixed(0) + ' B/s';
  if (bytesPerSec < 1048576) return (bytesPerSec / 1024).toFixed(0) + ' KB/s';
  return (bytesPerSec / 1048576).toFixed(1) + ' MB/s';
}

function formatDuration(ms) {
  const s = Math.floor(ms / 1000);
  if (s < 60) return s + 's';
  const m = Math.floor(s / 60);
  return m + 'm ' + (s % 60) + 's';
}

/* ── Port / Connect ── */

const PORT_FILTERS = [
  { usbVendorId: 0x303A },  /* Espressif */
  { usbVendorId: 0x1A86 },  /* QinHeng CHxxx */
  { usbVendorId: 0x10C4 },  /* Silicon Labs CP210x */
  { usbVendorId: 0x0403 },  /* FTDI */
];

async function connectToPort(port) {
  clearStatus();
  clearError();

  state.port = port;
  const transport = new Transport(port, true);
  state.transport = transport;

  const loader = new ESPLoader({
    transport,
    baudrate: 115200,
    terminal: {
      clean: () => {},
      writeLine: () => {},
      write: () => {},
    },
  });
  state.loader = loader;

  status('Detecting chip...', 'info');
  const chipName = await loader.main();
  state.chipName = chipName;

  if (!chipName.toLowerCase().includes('esp32')) {
    await disconnect();
    showError('Unsupported chip: ' + chipName + '. ESP32-S3 required.');
    return;
  }

  const [desc, mac, revision] = await Promise.all([
    loader.chip.getChipDescription(loader).catch(() => chipName),
    (async () => {
      try {
        const mac0 = await loader.readReg(0x5C002000);
        const mac1 = await loader.readReg(0x5C002004);
        const macNum = mac0 | ((mac1 & 0xFFFF) << 32);
        const bytes = [
          (macNum >>  0) & 0xFF, (macNum >>  8) & 0xFF,
          (macNum >> 16) & 0xFF, (macNum >> 24) & 0xFF,
          (macNum >> 32) & 0xFF, (macNum >> 40) & 0xFF,
        ];
        return bytes.map(b => b.toString(16).padStart(2, '0')).join(':').toUpperCase();
      } catch { return ''; }
    })(),
    loader.chip.getChipRevision(loader).catch(() => -1),
  ]);
  state.chipDesc = desc;
  state.mac = mac;
  state.chipRevision = revision;

  let flashSize = '';
  try {
    const fs = await loader.detectFlashSize();
    flashSize = String(fs || '');
  } catch { flashSize = ''; }
  state.flashSize = flashSize;

  clearStatus();
  els['chip-name'].textContent = chipName;
  els['chip-desc'].textContent = desc || chipName;
  const revDisplay = revision > 0 ? 'v' + revision : (desc.match(/revision\s+v?[\d.]+/i) || [''])[0] || '';
  els['chip-revision'].textContent = revDisplay || '—';
  els['chip-mac'].textContent = mac || '—';
  els['chip-flash-size'].textContent = flashSize || '—';
  els['chip-info-card'].classList.remove('hidden');

  els['btn-connect'].textContent = 'Connected';
  els['btn-connect'].disabled = false;
  els['btn-connect-prev'].classList.add('hidden');
  els['btn-change-port'].classList.remove('hidden');

  showStep(2);
  els['current-fw-version'].textContent = 'Checking...';

  await fetchReleases(true);
}

async function handleConnect() {
  if (!navigator.serial) {
    showError('WebSerial not supported. Open this page in Chrome or Edge version 89+.');
    return;
  }

  els['btn-connect'].disabled = true;
  els['btn-connect'].textContent = 'Connecting...';

  try {
    const port = await navigator.serial.requestPort({ filters: PORT_FILTERS });
    await connectToPort(port);
  } catch (err) {
    if (err instanceof DOMException && err.name === 'NotFoundError') {
      status('No port selected.', 'info');
    } else {
      showError('Connection failed: ' + err.message, err.stack);
    }
    els['btn-connect'].textContent = 'Connect ESP32-S3';
    els['btn-connect'].disabled = false;
  }
}

async function handleReconnect() {
  if (state.prevPorts.length === 0) return;
  const port = state.prevPorts[0];
  els['btn-connect-prev'].disabled = true;
  els['btn-connect-prev'].textContent = 'Reconnecting...';
  try {
    await connectToPort(port);
  } catch (err) {
    showError('Reconnect failed: ' + err.message, err.stack);
    els['btn-connect-prev'].textContent = 'Reconnect';
    els['btn-connect-prev'].disabled = false;
  }
}

async function handleChangePort() {
  try {
    const port = await navigator.serial.requestPort({ filters: PORT_FILTERS });
    await disconnect();
    await connectToPort(port);
  } catch (err) {
    if (!(err instanceof DOMException && err.name === 'NotFoundError')) {
      showError('Port change failed: ' + err.message);
    }
  }
}

async function disconnect() {
  try {
    if (state.loader) await state.loader.after('hard_reset');
  } catch {}
  try {
    if (state.transport) await state.transport.disconnect();
  } catch {}
  state.loader = null;
  state.transport = null;
  state.port = null;
}

/* ── Auto-detect previous ports ── */

async function checkPreviousPorts() {
  if (!navigator.serial) return;
  try {
    const ports = await navigator.serial.getPorts();
    state.prevPorts = ports;
    if (ports.length > 0) {
      const info = ports[0].getInfo();
      const label = info.usbVendorId === 0x303A ? 'ESP32-S3' : `USB device (${info.usbVendorId.toString(16).padStart(4, '0')})`;
      els['btn-connect-prev'].textContent = `Reconnect ${label}`;
      els['btn-connect-prev'].classList.remove('hidden');
    }
  } catch {}
}

/* ── Fetch Releases ── */

async function fetchReleases(detectInstalled) {
  els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">Loading releases...</td></tr>';
  els['release-count'].textContent = 'Loading releases...';

  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    const r = await fetch(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases?per_page=20`, { signal: controller.signal });
    clearTimeout(timeout);
    if (!r.ok) throw new Error('GitHub API error: ' + r.status);
    const releases = await r.json();
    state.releases = releases;

    if (releases.length === 0) {
      els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">No releases found.</td></tr>';
      els['release-count'].textContent = '0 releases';
      return;
    }

    els['release-count'].textContent = releases.length + ' release(s)';

    let installedMd5 = null;
    if (detectInstalled && state.loader) {
      try {
        status('Reading current firmware signature...', 'info');
        const appSize = 0x400000;
        installedMd5 = await state.loader.flashMd5sum(0x10000, appSize);
      } catch {
        installedMd5 = null;
      }
      clearStatus();
    }

    renderReleaseTable(releases, installedMd5);

  } catch (err) {
    els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:var(--color-error);text-align:center;padding:20px;">Failed to load releases: ' + err.message + '</td></tr>';
    els['release-count'].textContent = 'Release load failed';
  }
}

function renderReleaseTable(releases, installedMd5) {
  const tbody = els['release-table-body'];
  tbody.innerHTML = '';

  const assetMap = {};
  for (const rel of releases) {
    const asset = (rel.assets || []).find(a => a.name.startsWith(FIRMWARE_BUNDLE_PREFIX) && a.name.endsWith('.zip'));
    if (asset) {
      assetMap[rel.tag_name] = { release: rel, asset };
    }
  }

  const sortedTags = Object.keys(assetMap).sort((a, b) => {
    const va = a.replace(/^v/, '').split('.').map(Number);
    const vb = b.replace(/^v/, '').split('.').map(Number);
    for (let i = 0; i < Math.max(va.length, vb.length); i++) {
      const da = va[i] || 0, db = vb[i] || 0;
      if (da !== db) return db - da;
    }
    return 0;
  });

  let selectedTag = null;
  state.installedRelease = null;

  for (const tag of sortedTags) {
    const { release, asset } = assetMap[tag];
    const tr = document.createElement('tr');
    tr.dataset.tag = tag;
    tr.dataset.assetId = asset.id;

    if (!selectedTag) selectedTag = tag;

    tr.innerHTML = `
      <td><strong>${tag}</strong></td>
      <td>${formatDate(release.published_at)}</td>
      <td>${formatBytes(asset.size)}</td>
      <td class="mono">${asset.name}</td>
    `;

    tr.addEventListener('click', () => selectRelease(tag, tr));
    tbody.appendChild(tr);
  }

  state.selectedRelease = selectedTag;
  const rows = tbody.querySelectorAll('tr');
  for (const row of rows) {
    if (row.dataset.tag === selectedTag) {
      row.classList.add('selected');
    }
  }
  updateSelectedVersionInfo();
}

function selectRelease(tag, row) {
  state.selectedRelease = tag;
  const rows = els['release-table-body'].querySelectorAll('tr');
  rows.forEach(r => r.classList.remove('selected'));
  if (row) row.classList.add('selected');
  updateSelectedVersionInfo();
}

function updateSelectedVersionInfo() {
  const rel = state.releases.find(r => r.tag_name === state.selectedRelease);
  if (rel) {
    els['selected-version-info'].textContent = 'Selected: ' + rel.tag_name;
  }
}

/* ── Flash ── */

async function handleFlash() {
  clearStatus();
  clearError();

  if (!state.selectedRelease) {
    showError('Select a firmware version first.');
    return;
  }

  els['btn-flash'].disabled = true;
  els['btn-flash'].textContent = 'Flashing...';
  showStep(3);
  state.flashStartTime = Date.now();

  try {
    status('Downloading firmware...', 'info');

    // Download each firmware file from raw.githubusercontent.com (CORS-enabled)
    const baseUrl = `https://raw.githubusercontent.com/${GITHUB_OWNER}/${GITHUB_REPO}/${state.selectedRelease}/docs/flash/firmware`;
    const fileArray = [];
    const fileInfo = {};

    for (const name of FIRMWARE_FILES) {
      const url = `${baseUrl}/${name}`;
      const resp = await fetch(url);
      if (!resp.ok) {
        console.warn('Missing firmware file:', name, resp.status);
        continue;
      }
      const blob = await resp.blob();
      const data = new Uint8Array(await blob.arrayBuffer());
      const addr = FIRMWARE_OFFSETS[name];
      fileArray.push({ data, address: addr });
      fileInfo[name] = { data, size: data.length, address: addr };
    }

    if (fileArray.length === 0) throw new Error('No firmware files could be downloaded.');

    const progressArea = els['progress-area'];
    progressArea.innerHTML = '';
    const progressBars = {};

    for (const name of FIRMWARE_FILES) {
      if (!fileInfo[name]) continue;
      const row = document.createElement('div');
      row.className = 'progress-row';
      row.id = 'progress-' + name.replace(/[.]/g, '-');
      row.innerHTML = `
        <div class="progress-label">
          <span>${name}  <span class="mono" style="font-weight:400">0x${fileInfo[name].address.toString(16).toUpperCase()}</span></span>
          <span><span class="pct">0</span>%  <span class="rate">--</span></span>
        </div>
        <div class="progress-bar"><div class="progress-fill" style="width:0%"></div></div>
      `;
      progressArea.appendChild(row);
      progressBars[name] = row;
    }

    const totalSize = fileArray.reduce((s, f) => s + f.data.length, 0);
    let totalWritten = 0;

    status('Flashing firmware...', 'info');

    await state.loader.writeFlash({
      fileArray,
      flashMode: FLASH_CONFIG.flashMode,
      flashFreq: FLASH_CONFIG.flashFreq,
      flashSize: FLASH_CONFIG.flashSize,
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const name = FIRMWARE_FILES[fileIndex];
        if (!name) return;

        const row = progressBars[name];
        if (!row) return;

        const pct = total > 0 ? (written / total * 100) : 0;
        const fill = row.querySelector('.progress-fill');
        const pctEl = row.querySelector('.pct');
        const rateEl = row.querySelector('.rate');

        if (fill) fill.style.width = Math.min(pct, 100) + '%';
        if (pctEl) pctEl.textContent = pct.toFixed(0);

        if (written > 0 && state.flashStartTime > 0) {
          const elapsed = (Date.now() - state.flashStartTime) / 1000;
          if (elapsed > 0) {
            const bps = totalWritten / elapsed;
            if (rateEl) rateEl.textContent = formatRate(bps);
          }
        }

        if (fileIndex > 0) {
          const prevTotal = fileArray.slice(0, fileIndex).reduce((s, ff) => s + ff.data.length, 0);
          totalWritten = prevTotal + written;
        } else {
          totalWritten = written;
        }

        const totalPct = (totalWritten / totalSize * 100);
        els['flash-total-progress2'].style.width = Math.min(totalPct, 100) + '%';
        els['flash-total-label2'].textContent = `Total: ${formatBytes(totalWritten)} / ${formatBytes(totalSize)} (${totalPct.toFixed(0)}%)`;
      },
    });

    status('Flash complete!', 'success');
    await showSuccess();

  } catch (err) {
    showError('Flash failed: ' + err.message, err.stack);
    els['btn-flash'].disabled = false;
    els['btn-flash'].textContent = 'Flash Firmware';
    showStep(2);
  }
}

/* ── Verify (checksum of app partition) ── */

async function runVerification() {
  try {
    const appInfo = FIRMWARE_OFFSETS['NukCPGDrop.bin'];
    const expectedSize = 0x400000;
    await state.loader.flashMd5sum(appInfo, expectedSize);
  } catch {}
}

/* ── Success ── */

async function showSuccess() {
  const totalTime = Date.now() - state.flashStartTime;

  els['result-version'].textContent = state.selectedRelease;
  els['result-summary'].textContent = 'Firmware flashed.';

  const stats = els['result-stats'];
  stats.innerHTML = '';
  const items = [
    ['Target', state.chipName + ' (' + state.mac + ')'],
    ['Version', state.selectedRelease],
    ['Duration', formatDuration(totalTime)],
  ];
  for (const [label, value] of items) {
    stats.innerHTML += `<dt>${label}</dt><dd>${value}</dd>`;
  }

  showStep(4);

  try {
    await state.loader.after('hard_reset');
  } catch {}
  try {
    await state.transport.disconnect();
  } catch {}

  state.loader = null;
  state.transport = null;
  state.port = null;
}

/* ── UI Wiring ── */

document.addEventListener('DOMContentLoaded', () => {
  cacheElements();

  els['btn-connect'].addEventListener('click', handleConnect);
  els['btn-connect-prev'].addEventListener('click', handleReconnect);
  els['btn-change-port'].addEventListener('click', handleChangePort);
  els['btn-flash'].addEventListener('click', handleFlash);
  els['btn-refresh-releases'].addEventListener('click', () => fetchReleases(false));
  els['result-btn-again'].addEventListener('click', () => {
    showStep(1);
    els['btn-connect'].textContent = 'Connect ESP32-S3';
    els['btn-connect'].disabled = false;
    els['chip-info-card'].classList.add('hidden');
    els['btn-connect-prev'].classList.add('hidden');
    els['btn-change-port'].classList.add('hidden');
    checkPreviousPorts();
  });

  if (!navigator.serial) {
    showError('WebSerial is not supported by this browser. Open in Chrome or Edge.');
    els['btn-connect'].disabled = true;
  }

  checkPreviousPorts();
});
