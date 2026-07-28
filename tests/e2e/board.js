const qemu = require('./qemu');

const BOARD_TYPE = process.env.BOARD_TYPE || 'qemu';
const TARGET_URL = BOARD_TYPE === 'qemu' && !process.env.TARGET_URL
    ? 'http://localhost:8080'
    : (process.env.TARGET_URL || 'http://192.168.4.1');
const SERIAL_PORT = process.env.SERIAL_PORT || '';

function detectBoard() {
    const type = BOARD_TYPE.toLowerCase();
    const isQemu = type === 'qemu';
    const isDevKitC = type === 'devkitc' || type === 'e2eboard';
    const isDisplay = type === 'display' || type === 'displayboard';
    return {
        type,
        url: TARGET_URL,
        port: SERIAL_PORT,
        isQemu,
        isDevKitC,
        isDisplay,
        isHardware: !isQemu,
        label: isQemu ? 'QEMU (ESP32-S3)' : isDevKitC ? 'E2EBoard (DevKitC N8R2)' : 'DisplayBoard (N16R8)',
    };
}

function boardName() {
    return detectBoard().label;
}

function skipIfNot(mode) {
    const board = detectBoard();
    if (mode === 'qemu' && !board.isQemu) return true;
    if (mode === 'hardware' && !board.isHardware) return true;
    if (mode === 'devkitc' && !board.isDevKitC) return true;
    if (mode === 'display' && !board.isDisplay) return true;
    return false;
}

function skipReason(mode) {
    const board = detectBoard();
    if (mode === 'qemu') return 'Not in QEMU mode';
    if (mode === 'hardware') return 'Not in hardware mode';
    if (mode === 'devkitc') return 'DevKitC board not connected (set BOARD_TYPE=devkitc)';
    if (mode === 'display') return 'Display board not connected (set BOARD_TYPE=display)';
    return 'Board-specific test skipped';
}

async function startSerial() {
    if (BOARD_TYPE === 'qemu') {
        return qemu;
    }
    return null;
}

module.exports = {
    detectBoard,
    boardName,
    skipIfNot,
    skipReason,
    startSerial,
    QEMU_DIR: qemu.QEMU_DIR,
    SERIAL_LOG: qemu.SERIAL_LOG,
};
