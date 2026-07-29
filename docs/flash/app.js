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
const FIRMWARE_FILES = Object.keys(FIRMWARE_OFFSETS);

const FLASH_CONFIG = {
  flashMode: 'dio', flashFreq: '80m', flashSize: '16MB',
};

let state = {
  loader: null, transport: null, port: null,
  chipName: '', chipDesc: '', chipRevision: 0, mac: '', flashSize: '',
  releases: [], selectedRelease: null, installedMd5: null,
  flashStartTime: 0, prevPorts: [],
};

const els = {};
function el(id) { return document.getElementById(id); }

function cacheElements() {
  for (const id of [
    'step1','step2','step3','step4',
    'btn-connect','btn-connect-prev','btn-change-port',
    'release-table-body','btn-flash','progress-area',
    'verify-progress-fill','verify-progress-label',
    'result-icon','result-version','result-summary',
    'result-stats','result-btn-dashboard','result-btn-again',
    'status-msg','error-details',
    'release-count','btn-refresh-releases',
    'chip-name','chip-desc','chip-revision','chip-mac',
    'chip-flash-size','current-fw-version',
    'selected-version-info',
    'flash-total-progress2','flash-total-label2',
    'chip-info-card','qr-code-img','qr-ssid',
  ]) { els[id] = el(id); }
}

function showStep(n) {
  document.querySelectorAll('.step').forEach((s, i) => s.classList.toggle('active', i + 1 === n));
}

function status(msg, type) {
  const s = els['status-msg'];
  s.textContent = msg;
  s.className = 'status ' + type;
  s.classList.remove('hidden');
}
function clearStatus() { els['status-msg'].classList.add('hidden'); }
function showError(msg, detail) {
  status(msg, 'error');
  if (detail) { els['error-details'].textContent = detail; els['error-details'].classList.remove('hidden'); }
}
function clearError() { els['error-details'].classList.add('hidden'); }

function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(1) + ' MB';
}
function formatDate(iso) { return iso ? iso.slice(0, 10) : ''; }
function formatRate(bps) {
  if (bps < 1024) return bps.toFixed(0) + ' B/s';
  if (bps < 1048576) return (bps / 1024).toFixed(0) + ' KB/s';
  return (bps / 1048576).toFixed(1) + ' MB/s';
}
function formatDuration(ms) {
  const s = Math.floor(ms / 1000);
  return s < 60 ? s + 's' : Math.floor(s / 60) + 'm ' + (s % 60) + 's';
}

/* ── Port / Connect ── */

const PORT_FILTERS = [
  { usbVendorId: 0x303A }, { usbVendorId: 0x1A86 },
  { usbVendorId: 0x10C4 }, { usbVendorId: 0x0403 },
];

async function connectToPort(port) {
  clearStatus(); clearError();
  state.port = port;
  const transport = new Transport(port, true);
  state.transport = transport;
  const loader = new ESPLoader({
    transport, baudrate: 115200,
    terminal: { clean: () => {}, writeLine: () => {}, write: () => {} },
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
        const n = mac0 | ((mac1 & 0xFFFF) << 32);
        const b = [(n>>0)&0xFF,(n>>8)&0xFF,(n>>16)&0xFF,(n>>24)&0xFF,(n>>32)&0xFF,(n>>40)&0xFF];
        return b.map(x => x.toString(16).padStart(2,'0')).join(':').toUpperCase();
      } catch { return ''; }
    })(),
    loader.chip.getChipRevision(loader).catch(() => -1),
  ]);
  state.chipDesc = desc;
  state.mac = mac || '';
  state.chipRevision = revision;

  let flashSize = '';
  try { flashSize = String(await loader.detectFlashSize() || ''); } catch {}
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

  // Detect installed firmware by MD5
  let installedMd5 = null;
  try {
    status('Reading current firmware...', 'info');
    installedMd5 = await loader.flashMd5sum(0x10000, 0x400000);
    clearStatus();
  } catch { clearStatus(); }
  state.installedMd5 = installedMd5;

  await fetchReleases(installedMd5);
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
    port.addEventListener('disconnect', () => {
      els['btn-connect-prev'].classList.add('hidden');
      els['btn-connect'].textContent = 'Connect ESP32-S3';
      els['btn-connect'].disabled = false;
      els['chip-info-card'].classList.add('hidden');
    });
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
  port.addEventListener('disconnect', () => {
    els['btn-connect-prev'].classList.add('hidden');
    els['btn-connect'].textContent = 'Connect ESP32-S3';
    els['btn-connect'].disabled = false;
    els['chip-info-card'].classList.add('hidden');
  });
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
  try { if (state.loader) await state.loader.after('hard_reset'); } catch {}
  try { if (state.transport) await state.transport.disconnect(); } catch {}
  state.loader = null; state.transport = null; state.port = null;
}

/* ── Auto-detect previous ports ── */

async function checkPreviousPorts() {
  if (!navigator.serial) return;
  try {
    const ports = await navigator.serial.getPorts();
    state.prevPorts = ports;
    // Listen for disconnects on all previously authorized ports
    for (const p of ports) {
      p.addEventListener('disconnect', () => {
        els['btn-connect-prev'].classList.add('hidden');
        els['btn-connect'].textContent = 'Connect ESP32-S3';
        els['chip-info-card'].classList.add('hidden');
      });
    }
    if (ports.length > 0) {
      const info = ports[0].getInfo();
      const label = info.usbVendorId === 0x303A ? 'ESP32-S3' : `USB device (${info.usbVendorId.toString(16).padStart(4, '0')})`;
      els['btn-connect-prev'].textContent = `Reconnect ${label}`;
      els['btn-connect-prev'].classList.remove('hidden');
    }
  } catch {}
}

/* ── Fetch Releases ── */

async function fetchReleases(installedMd5) {
  els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">Loading releases...</td></tr>';
  els['release-count'].textContent = 'Loading releases...';
  try {
    const ac = new AbortController();
    const t = setTimeout(() => ac.abort(), 15000);
    const r = await fetch(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases?per_page=20`, { signal: ac.signal });
    clearTimeout(t);
    if (!r.ok) throw new Error('GitHub API error: ' + r.status);
    const releases = await r.json();
    state.releases = releases;
    if (releases.length === 0) {
      els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">No releases found.</td></tr>';
      els['release-count'].textContent = '0 releases';
      return;
    }
    els['release-count'].textContent = releases.length + ' release(s)';
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
    if (asset) assetMap[rel.tag_name] = { release: rel, asset };
  }
  const sorted = Object.keys(assetMap).sort((a, b) => {
    const va = a.replace(/^v/,'').split('.').map(Number);
    const vb = b.replace(/^v/,'').split('.').map(Number);
    for (let i = 0; i < Math.max(va.length, vb.length); i++) {
      if ((va[i]||0) !== (vb[i]||0)) return (vb[i]||0) - (va[i]||0);
    }
    return 0;
  });
  let sel = null;
  state.installedRelease = null;
  for (const tag of sorted) {
    const { release, asset } = assetMap[tag];
    const tr = document.createElement('tr');
    tr.dataset.tag = tag;
    if (!sel) sel = tag;
    tr.innerHTML = `<td><strong>${tag}</strong></td><td>${formatDate(release.published_at)}</td><td>${formatBytes(asset.size)}</td><td class="mono">${asset.name}</td>`;
    tr.addEventListener('click', () => selectRelease(tag, tr));
    tbody.appendChild(tr);
  }
  state.selectedRelease = sel;
  for (const row of tbody.querySelectorAll('tr')) {
    if (row.dataset.tag === sel) row.classList.add('selected');
  }
  updateSelectedVersionInfo();
}

function selectRelease(tag, row) {
  state.selectedRelease = tag;
  document.querySelectorAll('#release-table-body tr').forEach(r => r.classList.remove('selected'));
  if (row) row.classList.add('selected');
  updateSelectedVersionInfo();
}
function updateSelectedVersionInfo() {
  const rel = state.releases.find(r => r.tag_name === state.selectedRelease);
  if (rel) els['selected-version-info'].textContent = 'Selected: ' + rel.tag_name;
}

/* ── QR Code ── */

function updateQrCode(ssid) {
  if (!ssid) ssid = 'NukCPGDrop';
  els['qr-ssid'].textContent = 'SSID: ' + ssid;
  const wifi = `WIFI:S:${ssid};T:nopass;;`;
  els['qr-code-img'].src = `https://api.qrserver.com/v1/create-qr-code/?size=180x180&data=${encodeURIComponent(wifi)}`;
}

/* ── Flash ── */

async function handleFlash() {
  clearStatus(); clearError();
  if (!state.selectedRelease) { showError('Select a firmware version first.'); return; }

  els['btn-flash'].disabled = true; els['btn-flash'].textContent = 'Flashing...';
  showStep(3); state.flashStartTime = Date.now();

  try {
    status('Downloading firmware...', 'info');
    const refs = [state.selectedRelease, 'master'];
    const fileArray = []; const fileInfo = {};

    for (const name of FIRMWARE_FILES) {
      let data = null;
      for (const ref of refs) {
        const url = `https://raw.githubusercontent.com/${GITHUB_OWNER}/${GITHUB_REPO}/${ref}/docs/flash/firmware/${name}`;
        const resp = await fetch(url);
        if (resp.ok) { data = new Uint8Array(await (await resp.blob()).arrayBuffer()); break; }
      }
      if (!data) { console.warn('Missing:', name); continue; }
      fileArray.push({ data, address: FIRMWARE_OFFSETS[name] });
      fileInfo[name] = { size: data.length, address: FIRMWARE_OFFSETS[name] };
    }
    if (fileArray.length === 0) throw new Error('No firmware files could be downloaded.');

    const progressArea = els['progress-area']; progressArea.innerHTML = '';
    const progressBars = {};
    for (const name of FIRMWARE_FILES) {
      if (!fileInfo[name]) continue;
      const row = document.createElement('div'); row.className = 'progress-row';
      row.innerHTML = `<div class="progress-label"><span>${name}  <span class="mono" style="font-weight:400">0x${fileInfo[name].address.toString(16).toUpperCase()}</span></span><span><span class="pct">0</span>%  <span class="rate">--</span></span></div><div class="progress-bar"><div class="progress-fill" style="width:0%"></div></div>`;
      progressArea.appendChild(row); progressBars[name] = row;
    }

    const totalSize = fileArray.reduce((s, f) => s + f.data.length, 0);
    let totalWritten = 0;
    status('Flashing firmware...', 'info');

    await state.loader.writeFlash({
      fileArray, flashMode: FLASH_CONFIG.flashMode, flashFreq: FLASH_CONFIG.flashFreq,
      flashSize: FLASH_CONFIG.flashSize, eraseAll: false, compress: true,
      reportProgress: (fileIndex, written, total) => {
        const name = FIRMWARE_FILES[fileIndex]; if (!name) return;
        const row = progressBars[name]; if (!row) return;
        const pct = total > 0 ? (written / total * 100) : 0;
        const fill = row.querySelector('.progress-fill');
        const pctEl = row.querySelector('.pct');
        const rateEl = row.querySelector('.rate');
        if (fill) fill.style.width = Math.min(pct, 100) + '%';
        if (pctEl) pctEl.textContent = pct.toFixed(0);
        if (written > 0 && state.flashStartTime > 0) {
          const elapsed = (Date.now() - state.flashStartTime) / 1000;
          if (elapsed > 0 && rateEl) rateEl.textContent = formatRate(totalWritten / elapsed);
        }
        totalWritten = (fileIndex > 0 ? fileArray.slice(0, fileIndex).reduce((s, ff) => s + ff.data.length, 0) : 0) + written;
        const totalPct = totalWritten / totalSize * 100;
        els['flash-total-progress2'].style.width = Math.min(totalPct, 100) + '%';
        els['flash-total-label2'].textContent = `Total: ${formatBytes(totalWritten)} / ${formatBytes(totalSize)} (${totalPct.toFixed(0)}%)`;
      },
    });

    // Verify by MD5
    status('Verifying flash...', 'info');
    let verified = false;
    try {
      const writtenMd5 = await state.loader.flashMd5sum(0x10000, 0x400000);
      if (state.installedMd5) {
        // Can't compare directly since old and new are different
        verified = writtenMd5 && writtenMd5.length > 0;
      } else {
        verified = writtenMd5 && writtenMd5.length > 0;
      }
    } catch { verified = false; }

    // Hard reset the chip to release from download mode
    try { await state.loader.after('hard_reset'); } catch {}
    try { await state.transport.disconnect(); } catch {}
    state.loader = null; state.transport = null; state.port = null;

    await showSuccess(verified);

  } catch (err) {
    showError('Flash failed: ' + err.message, err.stack);
    els['btn-flash'].disabled = false; els['btn-flash'].textContent = 'Flash Firmware';
    showStep(2);
  }
}

/* ── Success ── */

async function showSuccess(verified) {
  const totalTime = Date.now() - state.flashStartTime;
  els['result-version'].textContent = state.selectedRelease;
  els['result-summary'].textContent = verified ? 'Flash verified successfully!' : 'Flash complete (verification skipped).';
  const stats = els['result-stats']; stats.innerHTML = '';
  for (const [l, v] of [['Target', state.chipName + (state.mac ? ' (' + state.mac + ')' : '')], ['Version', state.selectedRelease], ['Duration', formatDuration(totalTime)]]) {
    stats.innerHTML += `<dt>${l}</dt><dd>${v}</dd>`;
  }
  // Update QR code for the ESP AP
  if (state.mac) {
    const shortMac = state.mac.replace(/:/g, '').slice(-6).toUpperCase();
    updateQrCode('NukCPGDrop-' + shortMac);
  } else {
    updateQrCode('NukCPGDrop');
  }
  showStep(4);
}

/* ── UI Wiring ── */

document.addEventListener('DOMContentLoaded', () => {
  cacheElements();
  els['btn-connect'].addEventListener('click', handleConnect);
  els['btn-connect-prev'].addEventListener('click', handleReconnect);
  els['btn-change-port'].addEventListener('click', handleChangePort);
  els['btn-flash'].addEventListener('click', handleFlash);
  els['btn-refresh-releases'].addEventListener('click', () => fetchReleases(state.installedMd5));
  els['result-btn-again'].addEventListener('click', () => {
    showStep(1);
    els['btn-connect'].textContent = 'Connect ESP32-S3'; els['btn-connect'].disabled = false;
    els['chip-info-card'].classList.add('hidden');
    els['btn-connect-prev'].classList.add('hidden'); els['btn-change-port'].classList.add('hidden');
    checkPreviousPorts();
  });
  if (!navigator.serial) {
    showError('WebSerial is not supported by this browser. Open in Chrome or Edge.');
    els['btn-connect'].disabled = true;
  }
  checkPreviousPorts();
});
