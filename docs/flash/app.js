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
  chipFeatures: [],
  chipRevision: 0,
  mac: '',
  flashSize: '',
  releases: [],
  selectedRelease: null,
  firmwareFiles: null,
  installedRelease: null,
  flashStartTime: 0,
  verifyStartTime: 0,
};

const steps = document.querySelectorAll('.step');
const els = {};

function el(id) { return document.getElementById(id); }

function cacheElements() {
  const ids = [
    'step1', 'step2', 'step3', 'step4', 'step5', 'step6',
    'btn-connect', 'release-table-body',
    'btn-flash',
    'progress-area',
    'verify-progress-fill', 'verify-progress-label',
    'result-icon', 'result-version', 'result-summary',
    'result-stats', 'result-btn-dashboard', 'result-btn-again',
    'status-msg', 'error-details',
    'release-count', 'btn-refresh-releases',
    'chip-name', 'chip-desc', 'chip-revision', 'chip-mac',
    'chip-features', 'chip-flash-size', 'current-fw-version',
    'selected-version-info',     'flash-total-progress2',
    'flash-total-label2',
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

/* ── Step 1: Connect ── */

const PORT_FILTERS = [
  { usbVendorId: 0x303A },  /* Espressif */
  { usbVendorId: 0x1A86 },  /* QinHeng CHxxx */
  { usbVendorId: 0x10C4 },  /* Silicon Labs CP210x */
  { usbVendorId: 0x0403 },  /* FTDI */
];

async function handleConnect() {
  clearStatus();
  clearError();

  if (!navigator.serial) {
    showError('WebSerial not supported. Open this page in Chrome or Edge version 89+.');
    return;
  }

  els['btn-connect'].disabled = true;
  els['btn-connect'].textContent = 'Connecting...';

  try {
    const port = await navigator.serial.requestPort({ filters: PORT_FILTERS });
    state.port = port;

    // Transport auto-opens the port (don't call port.open() separately)
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

    const [desc, features, mac, revision] = await Promise.all([
      loader.chip.getChipDescription(loader).catch(() => ''),
      loader.chip.getChipFeatures(loader).catch(() => []),
      loader.chip.readMac(loader).catch(() => ''),
      loader.chip.getChipRevision(loader).catch(() => 0),
    ]);
    state.chipDesc = desc;
    state.chipFeatures = features;
    state.mac = mac;
    state.chipRevision = revision;

    let flashSize = '';
    try {
      const fs = await loader.detectFlashSize();
      flashSize = String(fs || '');
    } catch { flashSize = ''; }
    state.flashSize = flashSize;

    /* Show chip info */
    els['chip-name'].textContent = chipName;
    els['chip-desc'].textContent = desc || chipName;
    els['chip-revision'].textContent = 'v' + revision;
    els['chip-mac'].textContent = mac;
    els['chip-features'].textContent = features.join(', ') || '—';
    els['chip-flash-size'].textContent = flashSize || '—';

    /* Try to detect current firmware */
    els['current-fw-version'].textContent = 'Checking...';
    showStep(2);
    els['btn-connect'].textContent = 'Connected';
    els['btn-connect'].disabled = false;

    /* Auto-fetch releases and detect installed version */
    await fetchReleases(true);
    showStep(3);

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

/* ── Step 2 → 3: Fetch Releases ── */

async function fetchReleases(detectInstalled) {
  els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">Loading releases...</td></tr>';

  try {
    const r = await fetch(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases?per_page=20`);
    if (!r.ok) throw new Error('GitHub API error: ' + r.status);
    const releases = await r.json();
    state.releases = releases;

    if (releases.length === 0) {
      els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:#999;text-align:center;padding:20px;">No releases found.</td></tr>';
      return;
    }

    els['release-count'].textContent = releases.length + ' release(s)';

    /* If detecting installed firmware, compute MD5 of app partition */
    let installedMd5 = null;
    if (detectInstalled && state.loader) {
      try {
        status('Reading current firmware signature...', 'info');
        const appSize = 0x400000; /* 4MB — covers most firmware sizes */
        installedMd5 = await state.loader.flashMd5sum(0x10000, appSize);
      } catch {
        installedMd5 = null;
      }
      clearStatus();
    }

    renderReleaseTable(releases, installedMd5);

  } catch (err) {
    els['release-table-body'].innerHTML = '<tr><td colspan="4" style="color:var(--color-error);text-align:center;padding:20px;">Failed to load releases: ' + err.message + '</td></tr>';
  }
}

function renderReleaseTable(releases, installedMd5) {
  const tbody = els['release-table-body'];
  tbody.innerHTML = '';

  /* Build a map of release tag → firmware asset */
  const assetMap = {};
  for (const rel of releases) {
    const asset = (rel.assets || []).find(a => a.name.startsWith(FIRMWARE_BUNDLE_PREFIX) && a.name.endsWith('.zip'));
    if (asset) {
      assetMap[rel.tag_name] = { release: rel, asset };
    }
  }

  const sortedTags = Object.keys(assetMap).sort((a, b) => {
    /* Simple semver sort */
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
    tr.dataset.url = asset.browser_download_url;

    /* Check if this matches installed firmware */
    /* We'd need to download and extract manifest.json to compare, which is expensive.
       Instead, we'll just select the latest release by default. */
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

  /* Pre-select latest */
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

/* ── Step 4: Flash ── */

async function handleFlash() {
  clearStatus();
  clearError();

  if (!state.selectedRelease) {
    showError('Select a firmware version first.');
    return;
  }

  const release = state.releases.find(r => r.tag_name === state.selectedRelease);
  if (!release) {
    showError('Release not found.');
    return;
  }

  const asset = (release.assets || []).find(a => a.name.startsWith(FIRMWARE_BUNDLE_PREFIX));
  if (!asset) {
    showError('No firmware bundle found in this release.');
    return;
  }

  els['btn-flash'].disabled = true;
  els['btn-flash'].textContent = 'Flashing...';
  showStep(4);
  state.flashStartTime = Date.now();

  try {
    status('Downloading firmware bundle...', 'info');

    /* Download and extract the zip */
    const zipResp = await fetch(asset.browser_download_url);
    if (!zipResp.ok) throw new Error('Download failed: HTTP ' + zipResp.status);
    const zipBlob = await zipResp.blob();
    const zip = await JSZip.loadAsync(zipBlob);

    /* Read manifest */
    const manifestStr = await zip.file('manifest.json').async('string');
    const manifest = JSON.parse(manifestStr);

    const fileArray = [];
    const fileInfo = {};
    const fileOrder = ['bootloader.bin', 'partition-table.bin', 'NukCPGDrop.bin', 'ota_data_initial.bin'];

    for (const name of fileOrder) {
      const file = zip.file(name);
      if (!file) {
        console.warn('Missing file in bundle:', name);
        continue;
      }
      const data = new Uint8Array(await file.async('arraybuffer'));
      const addr = FIRMWARE_OFFSETS[name];
      fileArray.push({ data, address: addr });
      fileInfo[name] = { data, size: data.length, address: addr };
    }

    if (fileArray.length === 0) throw new Error('No firmware files found in bundle.');

    /* Initialize progress bars */
    const progressArea = els['progress-area'];
    progressArea.innerHTML = '';
    const progressBars = {};

    for (const name of fileOrder) {
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

    /* Total progress */
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
        const f = fileArray[fileIndex];
        const name = fileOrder[fileIndex];
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

        /* Track total written across files */
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

    status('Flash complete. Verifying...', 'info');
    await runVerification(manifest);

  } catch (err) {
    showError('Flash failed: ' + err.message, err.stack);
    els['btn-flash'].disabled = false;
    els['btn-flash'].textContent = 'Flash Firmware';
    showStep(3);
  }
}

/* ── Step 5: Verify ── */

async function runVerification(manifest) {
  state.verifyStartTime = Date.now();
  els['verify-progress-fill'].style.width = '0%';
  els['verify-progress-label'].textContent = 'Verifying flash integrity...';

  try {
    const appEntry = (manifest.files || []).find(f => f.name === 'NukCPGDrop.bin');
    if (!appEntry) throw new Error('No app entry in manifest');

    const appSize = appEntry.size;
    const expectedMd5 = appEntry.md5;

    els['verify-progress-fill'].style.width = '50%';
    els['verify-progress-label'].textContent = 'Computing flash checksum...';

    const actualMd5 = await state.loader.flashMd5sum(0x10000, appSize);

    els['verify-progress-fill'].style.width = '100%';
    els['verify-progress-label'].textContent = 'Flash verified.';

    if (actualMd5.toLowerCase() === expectedMd5.toLowerCase()) {
      await showSuccess(manifest);
    } else {
      showError('Verification failed: MD5 mismatch. Expected ' + expectedMd5 + ', got ' + actualMd5);
      els['verify-progress-fill'].classList.add('error');
      els['btn-flash'].disabled = false;
      els['btn-flash'].textContent = 'Flash Firmware';
    }

  } catch (err) {
    showError('Verification failed: ' + err.message, err.stack);
    els['verify-progress-fill'].classList.add('error');
    els['btn-flash'].disabled = false;
    els['btn-flash'].textContent = 'Flash Firmware';
  }
}

/* ── Step 6: Result ── */

async function showSuccess(manifest) {
  const totalTime = Date.now() - state.flashStartTime;

  els['result-version'].textContent = state.selectedRelease;
  els['result-summary'].textContent = 'Firmware flashed and verified.';

  const stats = els['result-stats'];
  stats.innerHTML = '';
  const items = [
    ['Target', state.chipName + ' (' + state.mac + ')'],
    ['From', state.installedRelease || 'unknown'],
    ['To', state.selectedRelease],
    ['Duration', formatDuration(totalTime)],
  ];
  for (const [label, value] of items) {
    stats.innerHTML += `<dt>${label}</dt><dd>${value}</dd>`;
  }

  showStep(6);

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

/* ── UI Event Wiring ── */

document.addEventListener('DOMContentLoaded', () => {
  cacheElements();

  els['btn-connect'].addEventListener('click', handleConnect);
  els['btn-flash'].addEventListener('click', handleFlash);
  els['btn-refresh-releases'].addEventListener('click', () => fetchReleases(false));
  els['result-btn-again'].addEventListener('click', () => {
    showStep(1);
    els['btn-connect'].textContent = 'Connect ESP32-S3';
    els['btn-connect'].disabled = false;
  });

  /* Check if WebSerial is available */
  if (!navigator.serial) {
    showError('WebSerial is not supported by this browser. Open in Chrome or Edge.');
    els['btn-connect'].disabled = true;
  }
});
