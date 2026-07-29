const { execSync, spawn } = require('child_process');
const path = require('path');

const QEMU_DIR = path.resolve(__dirname, '../../.qemu');
const QEMU_BIN = path.join(QEMU_DIR, 'qemu/bin/qemu-system-xtensa.exe');
const FLASH_BIN = path.join(QEMU_DIR, 'flash.bin');
const SERIAL_LOG = path.join(QEMU_DIR, 'qemu_serial.log');
const BOOT_TIMEOUT_MS = 20000;

let qemuProcess = null;

async function waitForBoot(timeoutMs = BOOT_TIMEOUT_MS) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        try {
            const fs = require('fs');
            const log = fs.readFileSync(SERIAL_LOG, 'utf8');
            if (log.includes('Ready. SSID:')) {
                const lines = log.split('\n').filter(l => l.includes('Ready. SSID:'));
                return { ok: true, log: lines[0] };
            }
        } catch { }
        await new Promise(r => setTimeout(r, 500));
    }
    const fs = require('fs');
    let lastLog = '';
    try { lastLog = fs.readFileSync(SERIAL_LOG, 'utf8').split('\n').slice(-10).join('\n'); } catch { }
    return { ok: false, error: 'Boot timeout', log: lastLog };
}

function createFlashImage() {
    const exec = require('child_process').execSync;
    const idfPath = process.env.IDF_PATH || 'C:/Users/thedy/source/repos/esp-idf';
    const buildDir = path.resolve(__dirname, '../../firmware/build');

    exec(`python -m esptool --chip esp32s3 merge_bin `
        + `--output "${FLASH_BIN}" --flash_mode dio --flash_freq 80m `
        + `--flash_size 16MB --fill-flash-size 16MB `
        + `0x0 "${buildDir}/bootloader/bootloader.bin" `
        + `0x8000 "${buildDir}/partition_table/partition-table.bin" `
        + `0x9000 "${QEMU_DIR}/nvs_partition.bin" `
        + `0xf000 "${QEMU_DIR}/phy_init.bin" `
        + `0x10000 "${buildDir}/NukCPGDrop.bin"`,
        { stdio: 'ignore', env: { ...process.env, PATH: `${process.env.PATH};${idfPath}/tools` } }
    );
}

function start() {
    if (qemuProcess) throw new Error('QEMU already running');
    const fs = require('fs');
    try { fs.unlinkSync(SERIAL_LOG); } catch { }

    qemuProcess = spawn(QEMU_BIN, [
        '-display', 'none',
        '-M', 'esp32s3',
        '-m', '2M',
        '-drive', `file=${FLASH_BIN},if=mtd,format=raw`,
        '-nic', 'user,hostfwd=tcp::8080-:80,model=open_eth',
        '-serial', `file:${SERIAL_LOG}`
    ], {
        stdio: ['ignore', 'ignore', 'ignore'],
        detached: true
    });
    qemuProcess.unref();
    return qemuProcess.pid;
}

function stop() {
    if (qemuProcess) {
        try { process.kill(-qemuProcess.pid); } catch { }
        try { qemuProcess.kill('SIGKILL'); } catch { }
        qemuProcess = null;
    }
    // Also kill any stray QEMU processes
    try {
        const { execSync } = require('child_process');
        execSync('taskkill /f /im qemu-system-xtensa.exe 2>nul', { stdio: 'ignore' });
    } catch { }
}

function getSerialLog() {
    try {
        return require('fs').readFileSync(SERIAL_LOG, 'utf8');
    } catch {
        return '';
    }
}

module.exports = { start, stop, waitForBoot, createFlashImage, getSerialLog, QEMU_DIR, SERIAL_LOG };
