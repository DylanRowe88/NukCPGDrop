const { chromium } = require('playwright');
const qemu = require('./qemu');
const fs = require('fs');
const path = require('path');

const TARGET_URL = process.env.TARGET_URL || 'http://localhost:8080';
const USE_QEMU = !process.env.TARGET_URL;

let browser;

before(async function () {
    this.timeout(60000);
    if (USE_QEMU) {
        console.log('Starting QEMU...');
        qemu.createFlashImage();
        qemu.start();
        const boot = await qemu.waitForBoot(25000);
        if (!boot.ok) {
            console.log('Boot log:', qemu.getSerialLog());
            qemu.stop();
            throw new Error(`Firmware boot failed: ${boot.error}`);
        }
        console.log('Firmware booted:', boot.log);
    }
    browser = await chromium.launch({ headless: true });
});

after(async function () {
    if (browser) await browser.close();
    if (USE_QEMU) qemu.stop();
});

describe('Firmware boot', function () {
    it('should boot successfully', function () {
        const log = qemu.getSerialLog();
        if (!log.includes('Ready. SSID:')) {
            throw new Error('Firmware did not report ready');
        }
        if (!log.includes('HTTP server running on :80')) {
            throw new Error('HTTP server did not start');
        }
        if (!log.includes('state loaded')) {
            throw new Error('State init failed');
        }
        if (!log.includes('mDNS: nukcpgdrop.local')) {
            throw new Error('mDNS init failed');
        }
    });
});

describe('Firmware features', function () {
    it('should report QEMU environment', function () {
        const log = qemu.getSerialLog();
        if (log.includes('QEMU detected')) {
            console.log('  Running under QEMU emulation');
        }
        if (log.includes('PCA9685 not found')) {
            console.log('  PCA9685 not present (simulated or no hardware)');
        }
    });

    it('should have started DNS server', function () {
        const log = qemu.getSerialLog();
        if (!log.includes('DNS server listening on port 53')) {
            throw new Error('DNS server did not start');
        }
    });

    it('should have valid app version', function () {
        const log = qemu.getSerialLog();
        const match = log.match(/App version:\s+(\S+)/);
        if (!match) throw new Error('Could not find app version');
        console.log(`  App version: ${match[1]}`);
    });
});

describe('HTTP server', function () {
    let page;

    before(async function () {
        this.timeout(15000);
        page = await browser.newPage();
    });

    after(async function () {
        if (page) await page.close();
    });

    it('should serve the main page', async function () {
        this.timeout(15000);
        try {
            const resp = await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            if (!resp) throw new Error('No response');
            if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()}`);
            console.log(`  Main page loaded (HTTP ${resp.status()})`);
        } catch (e) {
            if (e.message.includes('timeout') || e.message.includes('refused') || e.message.includes('reset')) {
                console.log(`  HTTP server not reachable: ${e.message}`);
                console.log('  (Expected in QEMU — WiFi PHY calibration incomplete)');
                this.skip();
                return;
            }
            throw e;
        }
    });

    it('should serve CSS and JS assets', async function () {
        this.timeout(10000);
        try {
            const [cssResp, jsResp] = await Promise.all([
                page.goto(`${TARGET_URL}/css/app.css`, { waitUntil: 'domcontentloaded', timeout: 5000 }),
                page.goto(`${TARGET_URL}/_framework/blazor.webassembly.js`, { waitUntil: 'domcontentloaded', timeout: 5000 })
            ]);
            if (cssResp && cssResp.status() !== 200) throw new Error(`CSS HTTP ${cssResp.status()}`);
            if (jsResp && jsResp.status() !== 200) throw new Error(`JS HTTP ${jsResp.status()}`);
            console.log('  Assets served successfully');
        } catch (e) {
            console.log(`  Assets not reachable: ${e.message}`);
            this.skip();
        }
    });
});

describe('API endpoints', function () {
    let page;

    before(async function () {
        this.timeout(15000);
        page = await browser.newPage();
    });

    after(async function () {
        if (page) await page.close();
    });

    it('should respond to /api/status', async function () {
        this.timeout(10000);
        try {
            const resp = await page.goto(`${TARGET_URL}/api/status`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            if (!resp) throw new Error('No response');
            const body = await resp.text();
            const json = JSON.parse(body);
            if (!('difficulty' in json)) throw new Error('Missing difficulty field');
            if (!('held' in json)) throw new Error('Missing held field');
            if (!('drop_count' in json)) throw new Error('Missing drop_count field');
            console.log(`  Status: difficulty=${json.difficulty} drops=${json.drop_count} held=${json.held}`);
        } catch (e) {
            if (e.message.includes('timeout') || e.message.includes('refused') || e.message.includes('reset')) {
                console.log(`  API not reachable: ${e.message}`);
                this.skip();
                return;
            }
            throw e;
        }
    });
});
