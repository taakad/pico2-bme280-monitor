// Configuration resolution order (highest wins):
//   1. environment variables
//   2. config.json at the repo root (written by the installer form)
//   3. built-in defaults matching the local development setup
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const int = (value, fallback) => {
    const parsed = Number.parseInt(value ?? '', 10);
    return Number.isFinite(parsed) ? parsed : fallback;
};

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
let fileCfg = {};
try {
    fileCfg = JSON.parse(readFileSync(path.join(repoRoot, 'config.json'), 'utf8'));
} catch {
    // no config.json — fall back to env/defaults
}

const pick = (envValue, fileValue, fallback) =>
    envValue !== undefined ? envValue : (fileValue !== undefined ? fileValue : fallback);

export const settings = {
    pg: {
        host: pick(process.env.PGHOST, fileCfg.pg?.host, '/tmp'),
        port: int(pick(process.env.PGPORT, fileCfg.pg?.port, 5433), 5433),
        database: pick(process.env.PGDATABASE, fileCfg.pg?.database, 'pico_monitor'),
        user: pick(process.env.PGUSER, fileCfg.pg?.user, 'pico'),
        password: pick(process.env.PGPASSWORD, fileCfg.pg?.password, ''),
    },
    http: {
        port: int(pick(process.env.PORT, fileCfg.http?.port, 8787), 8787),
        host: pick(process.env.HOST, fileCfg.http?.host, '127.0.0.1'),
    },
    serial: {
        // Empty means auto-detect by USB vendor id.
        port: pick(process.env.PICO_PORT, fileCfg.serial?.port, ''),
        baud: int(pick(process.env.PICO_BAUD, fileCfg.serial?.baud, 115200), 115200),
        retryMs: int(pick(process.env.PICO_RETRY_MS, fileCfg.serial?.retryMs, 2000), 2000),
    },
    // Raspberry Pi vendor id; 0x0D28 is the BOOTSEL mass-storage interface.
    vendorIds: ['2E8A', '0D28'],
};
