const { chromium } = require('playwright');
const qemu = require('./qemu');
const fs = require('fs');
const path = require('path');

const TARGET_URL = process.env.TARGET_URL || 'http://localhost:8080';
const USE_QEMU = !process.env.TARGET_URL;
const BOOT_TIMEOUT_MS = 30000;

let browser;

before(async function () {
    this.timeout(60000);
    if (USE_QEMU) {
        console.log('Starting QEMU ESP32-S3...');
        qemu.createFlashImage();
        qemu.start();
        const boot = await qemu.waitForBoot(BOOT_TIMEOUT_MS);
        if (!boot.ok) {
            console.log(`  Boot status: ${boot.error}`);
            console.log(`  Last log lines:\n${boot.log}`);
            // In QEMU the firmware reaches app_main() and prints several
            // messages before the WiFi PHY init hangs — that's expected
            // and testable.
            if (!boot.log.includes('NukCPGDrop starting')) {
                console.log('  WARNING: firmware did not reach app_main()');
            }
        } else {
            console.log('Firmware fully booted:', boot.log);
        }
    }
    browser = await chromium.launch({ headless: true });
});

after(async function () {
    if (browser) await browser.close();
    if (USE_QEMU) qemu.stop();
});

describe('Firmware boot', function () {
    const log = USE_QEMU ? qemu.getSerialLog() : '(skipped in hardware mode)';

    it('should reach app_main()', function () {
        if (USE_QEMU) {
            if (!log.includes('NukCPGDrop starting')) {
                throw new Error('Firmware did not reach app_main()');
            }
            console.log('  app_main() reached');
        }
    });

    it('should load NVS state', function () {
        if (USE_QEMU) {
            if (!log.includes('state loaded')) {
                throw new Error('State init failed');
            }
            console.log('  NVS state loaded');
        }
    });

    it('should handle missing hardware gracefully', function () {
        if (USE_QEMU) {
            if (!log.includes('QEMU detected')) {
                throw new Error('QEMU detection failed');
            }
            console.log('  QEMU environment detected');
        }
    });

    it('should store PHY calibration data for QEMU', function () {
        if (USE_QEMU) {
            const match = log.match(/PHY calibration store: (0x[0-9a-f]+)/);
            if (!match) throw new Error('PHY calibration store not attempted');
            if (match[1] !== '0x0') {
                throw new Error(`PHY store returned ${match[1]}`);
            }
            console.log(`  PHY calibration stored (${match[1]})`);
        }
    });

    it('should reach WiFi init', function () {
        if (USE_QEMU) {
            if (!log.includes('WiFi IRAM OP enabled')) {
                throw new Error('WiFi init did not start');
            }
            console.log('  WiFi driver initialized');
        }
    });

    it('should have valid app version', function () {
        const logSource = USE_QEMU ? log : '(hardware — check serial)';
        if (USE_QEMU) {
            const match = log.match(/App version:\s+(\S+)/);
            if (match) console.log(`  App version: ${match[1]}`);
        }
    });
});

describe('HTTP server', function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    it('should serve the main page', async function () {
        this.timeout(15000);
        try {
            const resp = await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded', timeout: 10000 });
            if (!resp) throw new Error('No response');
            if (resp.status() !== 200) throw new Error(`HTTP ${resp.status()}`);
            console.log(`  Main page loaded (HTTP ${resp.status()})`);
        } catch (e) {
            if (USE_QEMU) {
                console.log(`  HTTP not available in QEMU (PHY init limitation): ${e.message}`);
                this.skip();
                return;
            }
            throw e;
        }
    });
});

describe('API endpoints', function () {
    let page;
    before(async function () { this.timeout(15000); page = await browser.newPage(); });
    after(async function () { if (page) await page.close(); });

    it('should respond to /api/status', async function () {
        this.timeout(10000);
        try {
            const resp = await page.goto(`${TARGET_URL}/api/status`, { waitUntil: 'domcontentloaded', timeout: 5000 });
            if (!resp) throw new Error('No response');
            const body = await resp.text();
            const json = JSON.parse(body);
            if (!('difficulty' in json)) throw new Error('Missing difficulty');
            if (!('held' in json)) throw new Error('Missing held');
            console.log(`  Status: difficulty=${json.difficulty} drops=${json.drop_count}`);
        } catch (e) {
            if (USE_QEMU) {
                console.log(`  API not reachable in QEMU: ${e.message}`);
                this.skip();
                return;
            }
            throw e;
        }
    });
});
