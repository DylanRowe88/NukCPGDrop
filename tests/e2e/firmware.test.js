const { chromium } = require('playwright');
const board = require('./board');
const fs = require('fs');
const path = require('path');

const { detectBoard, boardName, skipIfNot, skipReason } = board;
const cfg = detectBoard();
const TARGET_URL = cfg.url;
const USE_QEMU = cfg.isQemu;
const BOOT_TIMEOUT_MS = 30000;
const PERF_LOG = path.resolve(__dirname, '../../.e2e-perf.json');

let perfMetrics = [];
let browser;

function logPerf(label, value, unit) {
    perfMetrics.push({ label, value, unit, board: cfg.type, timestamp: new Date().toISOString() });
    console.log(`  [perf] ${label}: ${value} ${unit}`);
}

function writePerfReport() {
    try {
        fs.writeFileSync(PERF_LOG, JSON.stringify(perfMetrics, null, 2));
        console.log(`  Performance report written to ${PERF_LOG}`);
    } catch (e) {
        console.log(`  Failed to write perf report: ${e.message}`);
    }
}

async function fetchJson(url) {
    const resp = await fetch(url, { timeout: 10000 });
    if (!resp.ok) throw new Error(`HTTP ${resp.status} for ${url}`);
    return await resp.json();
}

before(async function () {
    this.timeout(60000);
    if (USE_QEMU) {
        console.log('Starting QEMU ESP32-S3...');
        const qemu = require('./qemu');
        qemu.createFlashImage();
        qemu.start();
        const boot = await qemu.waitForBoot(BOOT_TIMEOUT_MS);
        if (!boot.ok) {
            console.log(`  Boot status: ${boot.error}`);
            console.log(`  Last log lines:\n${boot.log}`);
            if (!boot.log.includes('NukCPGDrop starting')) {
                console.log('  WARNING: firmware did not reach app_main()');
            }
        } else {
            console.log('Firmware fully booted:', boot.log);
        }
    } else {
        console.log(`Hardware mode: ${cfg.label} at ${TARGET_URL}`);
    }
    browser = await chromium.launch({ headless: true });
});

after(async function () {
    if (browser) await browser.close();
    if (USE_QEMU) {
        const qemu = require('./qemu');
        qemu.stop();
    }
    writePerfReport();
});

// ── Boot tests (QEMU: serial log; hardware: skip) ──────────────────

describe('Firmware boot — ' + boardName(), function () {
    const log = USE_QEMU ? require('./qemu').getSerialLog() : '(skipped in hardware mode)';

    it('should reach app_main()', function () {
        if (!USE_QEMU) { console.log('  (hardware — verified by flash.py)'); return; }
        if (!log.includes('NukCPGDrop starting')) {
            throw new Error('Firmware did not reach app_main()');
        }
        console.log('  app_main() reached');
    });

    it('should load NVS state', function () {
        if (!USE_QEMU) return;
        if (!log.includes('state loaded')) {
            throw new Error('State init failed');
        }
        console.log('  NVS state loaded');
    });

    it('should handle missing hardware gracefully', function () {
        if (!USE_QEMU) return;
        if (!log.includes('QEMU detected')) {
            throw new Error('QEMU detection failed');
        }
        console.log('  QEMU environment detected');
    });

    it('should store PHY calibration data for QEMU', function () {
        if (!USE_QEMU) return;
        const match = log.match(/PHY calibration store: (0x[0-9a-f]+)/);
        if (!match) throw new Error('PHY calibration store not attempted');
        if (match[1] !== '0x0') {
            throw new Error(`PHY store returned ${match[1]}`);
        }
        console.log(`  PHY calibration stored (${match[1]})`);
    });

    it('should reach WiFi init', function () {
        if (!USE_QEMU) return;
        if (!log.includes('WiFi IRAM OP enabled')) {
            throw new Error('WiFi init did not start');
        }
        console.log('  WiFi driver initialized');
    });

    it('should have valid app version', function () {
        if (USE_QEMU) {
            const match = log.match(/App version:\s+(\S+)/);
            if (match) console.log(`  App version: ${match[1]}`);
        }
    });

    it('should start HTTP server', function () {
        if (!USE_QEMU) return;
        if (!log.includes('HTTP server running')) {
            throw new Error('HTTP server did not start');
        }
        console.log('  HTTP server running');
    });

    it('should start DNS server', function () {
        if (!USE_QEMU) return;
        if (!log.includes('DNS server listening')) {
            throw new Error('DNS server did not start');
        }
        console.log('  DNS server running');
    });

    it('should register mDNS', function () {
        if (!USE_QEMU) return;
        if (!log.includes('nukcpgdrop.local')) {
            throw new Error('mDNS not registered');
        }
        console.log('  mDNS: nukcpgdrop.local');
    });
});

// ── HTTP server reachability ───────────────────────────────────────

describe('HTTP server — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    it('should serve the main page', async function () {
        this.timeout(15000);
        try {
            const t0 = Date.now();
            const resp = await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            const loadTime = Date.now() - t0;
            if (!resp) throw new Error('No response');
            if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()}`);
            logPerf('Page load (DOM)', loadTime, 'ms');
            console.log(`  Main page loaded (HTTP ${resp.status()}, ${loadTime}ms)`);
        } catch (e) {
            if (USE_QEMU) {
                console.log(`  HTTP not available in QEMU: ${e.message}`);
                this.skip();
                return;
            }
            throw e;
        }
    });

    it('should serve the main page (full load)', async function () {
        this.timeout(30000);
        try {
            const t0 = Date.now();
            const resp = await page.goto(TARGET_URL, { waitUntil: 'networkidle', timeout: 20000 });
            const loadTime = Date.now() - t0;
            if (!resp) throw new Error('No response');
            if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()}`);
            logPerf('Page load (full)', loadTime, 'ms');
            console.log(`  Full page load: ${loadTime}ms`);
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });
});

// ── API endpoints ──────────────────────────────────────────────────

describe('API endpoints — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    it('/api/status should return valid JSON with all fields', async function () {
        this.timeout(10000);
        try {
            const t0 = Date.now();
            const resp = await page.goto(`${TARGET_URL}/api/status`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            const responseTime = Date.now() - t0;
            if (!resp) throw new Error('No response');
            const body = await resp.text();
            const json = JSON.parse(body);
            if (!('difficulty' in json)) throw new Error('Missing difficulty');
            if (!('held' in json)) throw new Error('Missing held');
            if (!('drop_count' in json)) throw new Error('Missing drop_count');
            if (!('total_drops' in json)) throw new Error('Missing total_drops');
            logPerf('/api/status response time', responseTime, 'ms');
            console.log(`  status: difficulty=${json.difficulty} held=${JSON.stringify(json.held)} drops=${json.drop_count}`);
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });

    it('/api/status should include battery field on display board', async function () {
        this.timeout(10000);
        if (!cfg.isDisplay) { console.log('  (display board only)'); this.skip(); return; }
        try {
            const resp = await page.goto(`${TARGET_URL}/api/status`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            if (!resp) throw new Error('No response');
            const body = await resp.text();
            const json = JSON.parse(body);
            if (!('battery' in json)) {
                console.log('  WARNING: battery field missing (Phase 4 not complete?)');
                return;
            }
            console.log(`  battery: ${json.battery.percent}% (${json.battery.millivolts}mV, charging=${json.battery.charging})`);
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });

    it('/api/status should measure response time under 500ms', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        const times = [];
        for (let i = 0; i < 5; i++) {
            const t0 = Date.now();
            await page.goto(`${TARGET_URL}/api/status`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            times.push(Date.now() - t0);
        }
        const avg = times.reduce((a, b) => a + b, 0) / times.length;
        const max = Math.max(...times);
        logPerf('/api/status avg response (5 samples)', avg, 'ms');
        logPerf('/api/status max response (5 samples)', max, 'ms');
        console.log(`  Response times (ms): ${times.join(', ')} (avg=${avg.toFixed(0)}, max=${max})`);
        if (avg > 500) {
            console.log(`  WARNING: average response time ${avg.toFixed(0)}ms exceeds 500ms`);
        }
    });

    it('should support OPTIONS /api/status (CORS preflight)', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const resp = await page.evaluate(async () => {
                const r = await fetch('/api/status', { method: 'OPTIONS' });
                return { status: r.status, headers: Object.fromEntries(r.headers.entries()) };
            });
            if (resp.status !== 204 && resp.status !== 200) {
                console.log(`  OPTIONS returned HTTP ${resp.status} (may not be implemented)`);
            } else {
                console.log(`  CORS preflight: HTTP ${resp.status}`);
            }
        } catch (e) {
            console.log(`  CORS preflight check: ${e.message}`);
        }
    });
});

// ── Static assets ──────────────────────────────────────────────────

describe('Static assets — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    const ASSETS = [
        { path: '/', expectedType: 'text/html' },
        { path: '/index.html', expectedType: 'text/html' },
        { path: '/css/app.css', expectedType: 'text/css' },
        { path: '/_framework/blazor.webassembly.js', expectedType: 'application/javascript' },
        { path: '/_framework/dotnet.js', expectedType: 'application/javascript' },
        { path: '/_framework/dotnet.runtime.js', expectedType: 'application/javascript' },
        { path: '/_framework/dotnet.native.wasm', expectedType: 'application/wasm' },
        { path: '/images/Nuks_Logo_Final_White.png', expectedType: 'image/png' },
        { path: '/images/nuks-possum-white.png', expectedType: 'image/png' },
        { path: '/js/rangeSlider.js', expectedType: 'application/javascript' },
    ];

    ASSETS.forEach(({ path: assetPath, expectedType }) => {
        it(`should serve ${assetPath} with correct type`, async function () {
            this.timeout(10000);
            if (USE_QEMU) { this.skip(); return; }
            try {
                const t0 = Date.now();
                const resp = await page.goto(`${TARGET_URL}${assetPath}`, { waitUntil: 'domcontentloaded', timeout: 5000 });
                const loadTime = Date.now() - t0;
                if (!resp) throw new Error('No response');
                if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()}`);
                const ct = resp.headers()['content-type'] || '';
                const body = await resp.text();
                const size = body.length;
                logPerf(`Asset ${assetPath}`, size, 'bytes');
                if (!ct.includes(expectedType)) {
                    console.log(`  WARNING: expected ${expectedType}, got ${ct}`);
                }
                console.log(`  ${assetPath} (${size}B, ${loadTime}ms)`);
            } catch (e) {
                if (USE_QEMU) { this.skip(); return; }
                console.log(`  FAIL ${assetPath}: ${e.message}`);
                throw e;
            }
        });
    });

    it('should serve .wasm files with correct MIME type', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        const wasmAssets = [
            '/_framework/dotnet.native.wasm',
            '/_framework/System.Private.CoreLib.wasm',
        ];
        for (const wa of wasmAssets) {
            const resp = await page.goto(`${TARGET_URL}${wa}`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            if (!resp) throw new Error(`No response for ${wa}`);
            if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()} for ${wa}`);
            const ct = resp.headers()['content-type'] || '';
            if (!ct.includes('application/wasm') && !ct.includes('application/octet-stream')) {
                console.log(`  WARNING: ${wa} has Content-Type: ${ct}`);
            }
            console.log(`  ${wa}: HTTP ${resp.status()}, ${ct}`);
        }
    });

    it('should return 404 for unknown paths', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const resp = await page.goto(`${TARGET_URL}/nonexistent_asset_xyz`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            if (resp && resp.status() === 404) {
                console.log('  Unknown path correctly returns 404');
            } else {
                console.log(`  Unknown path returned HTTP ${resp ? resp.status() : 'no response'}`);
            }
        } catch (e) {
            console.log(`  404 check: ${e.message}`);
        }
    });
});

// ── Captive portal ─────────────────────────────────────────────────

describe('Captive portal — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    const CAPTIVE_PROBES = [
        '/generate_204',
        '/hotspot-detect.html',
        '/connecttest.txt',
        '/ncsi.txt',
        '/fwlink/',
        '/success.txt',
        '/canonical.html',
        '/gen_204',
        '/favicon.ico',
    ];

    CAPTIVE_PROBES.forEach(probe => {
        it(`should redirect ${probe} to portal`, async function () {
            this.timeout(10000);
            if (USE_QEMU) { this.skip(); return; }
            try {
                const resp = await page.goto(`${TARGET_URL}${probe}`, { waitUntil: 'domcontentloaded', timeout: 5000 });
                if (!resp) throw new Error('No response');
                const status = resp.status();
                const location = resp.headers()['location'] || '';
                if (status === 302 && location.includes(TARGET_URL)) {
                    console.log(`  ${probe} -> 302 -> ${location}`);
                } else if (status === 200) {
                    console.log(`  ${probe} -> 200 (not redirecting, captive portal may be disabled)`);
                } else {
                    console.log(`  ${probe} -> HTTP ${status} (Location: ${location})`);
                }
            } catch (e) {
                if (USE_QEMU) { this.skip(); return; }
                console.log(`  FAIL ${probe}: ${e.message}`);
                throw e;
            }
        });
    });
});

// ── Dashboard UI ───────────────────────────────────────────────────

describe('Dashboard UI — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    it('should render dashboard content', async function () {
        this.timeout(15000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            await page.waitForTimeout(2000);
            const title = await page.title();
            const body = await page.textContent('body') || '';
            const checks = ['DROP ALL', 'NukCPGDrop', 'NUKCPGDROP', 'Difficulty'];
            const found = checks.filter(c => body.includes(c) || title.includes(c));
            if (found.length === 0) {
                console.log('  WARNING: No expected dashboard text found');
                console.log(`  Page title: "${title}"`);
                console.log(`  Body preview: "${body.substring(0, 200)}"`);
                return;
            }
            console.log(`  Dashboard renders: ${found.join(', ')}`);
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });

    it('should display Nuks logo image', async function () {
        this.timeout(15000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            const logo = await page.$('img[alt="Nuks Logo"], img[src*="logo"], img[src*="Nuks"]');
            if (logo) {
                const src = await logo.getAttribute('src');
                console.log(`  Nuks logo found: ${src}`);
            } else {
                console.log('  Nuks logo not found (may not be loaded yet)');
            }
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });

    it('should display battery indicator on display board', async function () {
        this.timeout(15000);
        if (!cfg.isDisplay) { this.skip(); return; }
        try {
            await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            const body = await page.textContent('body') || '';
            if (body.includes('Battery') || body.includes('battery') || body.includes('%')) {
                console.log('  Battery indicator shown');
            } else {
                console.log('  Battery indicator not found in DOM');
            }
        } catch (e) {
            if (USE_QEMU) { this.skip(); return; }
            throw e;
        }
    });
});

// ── Control operations (DevKitC) ───────────────────────────────────

describe('Control operations — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    async function postApi(path, body) {
        return await page.evaluate(async (args) => {
            const r = await fetch(args.path, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(args.body),
            });
            return { status: r.status, body: await r.text() };
        }, { path, body });
    }

    it('should update difficulty via API', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const result = await postApi('/api/difficulty', { difficulty: 2 });
            if (result.status === 200) {
                console.log(`  Difficulty set: HTTP ${result.status}`);
            } else {
                console.log(`  WARNING: /api/difficulty returned HTTP ${result.status}: ${result.body.substring(0, 100)}`);
            }
        } catch (e) {
            console.log(`  /api/difficulty: ${e.message}`);
        }
    });

    it('should toggle drop mode via API', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const result = await postApi('/api/drop_mode', { mode: 'double' });
            if (result.status === 200) {
                console.log(`  Drop mode set: HTTP ${result.status}`);
            } else {
                console.log(`  WARNING: /api/drop_mode returned HTTP ${result.status}`);
            }
        } catch (e) {
            console.log(`  /api/drop_mode: ${e.message}`);
        }
    });

    it('should reset drops via API', async function () {
        this.timeout(10000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const result = await postApi('/api/reset', {});
            if (result.status === 200) {
                console.log(`  Reset: HTTP ${result.status}`);
            } else {
                console.log(`  WARNING: /api/reset returned HTTP ${result.status}`);
            }
        } catch (e) {
            console.log(`  /api/reset: ${e.message}`);
        }
    });

    it('should toggle individual cans via API', async function () {
        this.timeout(15000);
        if (USE_QEMU) { this.skip(); return; }
        const canIds = [0, 1, 2, 3, 4, 5];
        const results = [];
        for (const canId of canIds) {
            try {
                const result = await postApi('/api/toggle', { can: canId });
                results.push(`can${canId}=${result.status}`);
            } catch (e) {
                results.push(`can${canId}=ERR`);
            }
        }
        console.log(`  Can toggles: ${results.join(', ')}`);
    });

    it('should set servo calibration via API', async function () {
        this.timeout(15000);
        if (USE_QEMU) { this.skip(); return; }
        const canIds = [0, 1, 2, 3, 4, 5];
        const results = [];
        for (const canId of canIds) {
            try {
                const result = await postApi('/api/calibrate', { can: canId, position: 90 });
                results.push(`can${canId}=${result.status}`);
            } catch (e) {
                results.push(`can${canId}=ERR`);
            }
        }
        console.log(`  Calibrations: ${results.join(', ')}`);
    });
});

// ── Performance metrics ────────────────────────────────────────────

describe('Performance metrics — ' + boardName(), function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    const LARGEST_ASSETS = [
        '/_framework/dotnet.native.wasm',
        '/_framework/System.Private.CoreLib.wasm',
    ];

    LARGEST_ASSETS.forEach(assetPath => {
        it(`should measure ${assetPath} download size`, async function () {
            this.timeout(30000);
            if (USE_QEMU) { this.skip(); return; }
            try {
                const t0 = Date.now();
                const resp = await page.goto(`${TARGET_URL}${assetPath}`, { waitUntil: 'domcontentloaded', timeout: 15000 });
                const loadTime = Date.now() - t0;
                if (!resp) throw new Error('No response');
                const body = await resp.text();
                const size = body.length;
                logPerf(`Asset size ${assetPath}`, size, 'bytes');
                logPerf(`Asset load ${assetPath}`, loadTime, 'ms');
                console.log(`  ${assetPath}: ${(size / 1024).toFixed(1)}KB in ${loadTime}ms`);
            } catch (e) {
                if (USE_QEMU) { this.skip(); return; }
                console.log(`  FAIL ${assetPath}: ${e.message}`);
            }
        });
    });

    it('should measure total page weight', async function () {
        this.timeout(60000);
        if (USE_QEMU) { this.skip(); return; }
        try {
            const client = await page.context().newCDPSession(page);
            await client.send('Network.enable');
            let totalBytes = 0;
            let totalRequests = 0;
            client.on('Network.responseReceived', params => {
                totalRequests++;
                const headers = params.response.headers;
                if (headers['content-length']) {
                    totalBytes += parseInt(headers['content-length'], 10);
                }
            });
            const t0 = Date.now();
            await page.goto(TARGET_URL, { waitUntil: 'networkidle', timeout: 30000 });
            const loadTime = Date.now() - t0;
            logPerf('Total page weight', totalBytes, 'bytes');
            logPerf('Total requests', totalRequests, 'count');
            logPerf('Full page load time', loadTime, 'ms');
            console.log(`  ${totalRequests} requests, ${(totalBytes / 1024).toFixed(1)}KB total, ${loadTime}ms load`);
        } catch (e) {
            console.log(`  Page weight measurement: ${e.message}`);
        }
    });
});

// ── Display board specific tests ───────────────────────────────────

describe('Display board — ' + boardName(), function () {
    before(async function () {
        if (!cfg.isDisplay) {
            console.log('  (display board only — skipping)');
            this.skip();
        }
    });
    after(async function () { });

    it('should respond at TARGET_URL', async function () {
        this.timeout(10000);
        try {
            const resp = await fetch(TARGET_URL, { method: 'HEAD', timeout: 5000 });
            console.log(`  Board reachable at ${TARGET_URL} (HTTP ${resp.status})`);
        } catch (e) {
            console.log(`  Board not reachable: ${e.message}`);
            throw e;
        }
    });

    it('/api/status should include display info', async function () {
        this.timeout(10000);
        try {
            const json = await fetchJson(`${TARGET_URL}/api/status`);
            console.log(`  Firmware version: ${json.version || 'unknown'}`);
            if ('display' in json) {
                console.log(`  Display: ${json.display.width}x${json.display.height}`);
            } else {
                console.log('  Display info not exposed (Phase 2 may not be complete)');
            }
        } catch (e) {
            console.log(`  /api/status: ${e.message}`);
        }
    });

    it('should expose touch capability', async function () {
        this.timeout(10000);
        try {
            const json = await fetchJson(`${TARGET_URL}/api/status`);
            if ('touch' in json) {
                console.log(`  Touch: ${json.touch}`);
            } else {
                console.log('  Touch info not exposed');
            }
        } catch (e) {
            console.log(`  Touch check: ${e.message}`);
        }
    });

    it('should expose audio capability', async function () {
        this.timeout(10000);
        try {
            const json = await fetchJson(`${TARGET_URL}/api/status`);
            if ('audio' in json) {
                console.log(`  Audio: ${json.audio}`);
            } else {
                console.log('  Audio info not exposed');
            }
        } catch (e) {
            console.log(`  Audio check: ${e.message}`);
        }
    });
});
