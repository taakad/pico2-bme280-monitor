import http from 'node:http';
import { connect, migrate, upsertDevice, insertMeasurements, saveSettings, close } from './db.js';
import { MonitorLink } from './serial.js';
import { createApp, attachWebSocket, DEFAULT_SETTINGS } from './api.js';
import { settings as env } from './config.js';

const state = {
    serial: { state: 'starting', port: null, message: '' },
    deviceId: null,
    devicePort: null,
    deviceConfig: null,
    samples: 0,
    startedAt: new Date().toISOString(),
};

// Samples are buffered and written in batches: one INSERT per batch keeps the
// database load trivial even at the fastest cadence (5 ms, ~170 samples/s).
const FLUSH_INTERVAL_MS = 2000;
let batch = [];
let flushTimer = null;

async function flush(deviceId, ws) {
    if (batch.length === 0 || !deviceId) {
        return;
    }
    const pending = batch;
    batch = [];
    try {
        await insertMeasurements(deviceId, pending);
        state.samples += pending.length;
        ws.broadcast('batch', { count: pending.length, total: state.samples });
    } catch (err) {
        console.error('[collector] insert failed:', err.message);
        // Re-queue so a database blip does not silently drop measurements.
        batch = pending.concat(batch).slice(-5000);
    }
}

async function main() {
    await connect();
    const schema = await migrate();
    console.log(`[db] schema applied: ${schema}`);

    const server = http.createServer();
    const ws = attachWebSocket(server, () => state);

    const link = new MonitorLink();

    link.addEventListener('status', (e) => {
        state.serial = { ...e.detail, at: new Date().toISOString() };
        console.log(`[serial] ${e.detail.state} ${e.detail.port ?? ''} ${e.detail.message ?? ''}`.trim());
        if (e.detail.state === 'connected') {
            link.requestConfig();
        }
        ws.broadcast('status', state.serial);
    });

    link.addEventListener('config', async (e) => {
        state.deviceConfig = { ...DEFAULT_SETTINGS, ...e.detail };
        console.log('[config]', JSON.stringify(state.deviceConfig));
        if (state.deviceId) {
            try {
                await saveSettings(state.deviceId, state.deviceConfig);
            } catch (err) {
                console.error('[db] settings save failed:', err.message);
            }
        }
        ws.broadcast('config', state.deviceConfig);
    });

    link.addEventListener('sample', async (e) => {
        const sample = { ...e.detail, ts: new Date() };
        // Attach the settings snapshot so each row explains its own provenance.
        if (state.deviceConfig) {
            sample.config = state.deviceConfig;
        }
        ws.broadcast('sample', sample);

        if (!state.deviceId) {
            try {
                const device = await upsertDevice(link.portPath);
                state.deviceId = device.id;
                state.devicePort = device.port;
                console.log(`[db] device #${device.id} on ${device.port}`);
            } catch (err) {
                console.error('[db] device upsert failed:', err.message);
                return;
            }
        }
        batch.push(sample);
    });

    const app = createApp({ link, getState: () => state });
    server.on('request', app);

    flushTimer = setInterval(() => flush(state.deviceId, ws), FLUSH_INTERVAL_MS);

    server.listen(env.http.port, env.http.host, () => {
        console.log(`[http] http://${env.http.host}:${env.http.port}  (ws: /ws)`);
    });
    link.start();

    const shutdown = async (signal) => {
        console.log(`[shutdown] ${signal}`);
        clearInterval(flushTimer);
        await flush(state.deviceId, ws);
        link.stop();
        ws.close();
        server.close();
        await close();
        process.exit(0);
    };
    process.on('SIGINT', () => shutdown('SIGINT'));
    process.on('SIGTERM', () => shutdown('SIGTERM'));
}

main().catch((err) => {
    console.error('[fatal]', err);
    process.exit(1);
});
