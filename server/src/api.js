import express from 'express';
import { WebSocketServer } from 'ws';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as db from './db.js';

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Mirrors the firmware's own validation so a bad request is rejected before it
// reaches the device. The firmware re-validates anyway; this is only to give
// the UI a clean error.
export const DEFAULT_SETTINGS = {
    interval_ms: 2000,
    decimals: 2,
    average: 1,
    osr_t: 1,
    osr_p: 1,
    osr_h: 1,
    filter: 0,
    mode: 'normal',
};

const SETTING_KEYS = Object.keys(DEFAULT_SETTINGS);

function pickSettings(obj) {
    const out = {};
    for (const key of SETTING_KEYS) {
        if (obj?.[key] !== undefined && obj[key] !== null) {
            out[key] = obj[key];
        }
    }
    return out;
}

export function validateSettings(body) {
    const errors = [];
    const out = { ...DEFAULT_SETTINGS };
    const num = (key, lo, hi) => {
        const raw = body?.[key];
        if (raw === undefined) {
            return;
        }
        const v = Number(raw);
        if (!Number.isInteger(v) || v < lo || v > hi) {
            errors.push(`${key} must be an integer in ${lo}..${hi}`);
            return;
        }
        out[key] = v;
    };
    num('interval_ms', 100, 3600000);
    num('decimals', 0, 6);
    num('average', 1, 64);
    for (const key of ['osr_t', 'osr_p', 'osr_h']) {
        const raw = body?.[key];
        if (raw === undefined) {
            continue;
        }
        const v = Number(raw);
        if (![1, 2, 4, 8, 16].includes(v)) {
            errors.push(`${key} must be 1, 2, 4, 8 or 16`);
        } else {
            out[key] = v;
        }
    }
    num('filter', 0, 4);
    if (body?.mode !== undefined) {
        if (!['normal', 'forced'].includes(body.mode)) {
            errors.push('mode must be normal or forced');
        } else {
            out.mode = body.mode;
        }
    }
    return { errors, settings: out };
}

export function createApp({ link, getState }) {
    const app = express();
    app.use(express.json());

    app.get('/api/health', (_req, res) => {
        const s = getState();
        res.json({ ok: true, serial: s.serial, device: s.device, samples: s.samples });
    });

    app.get('/api/devices', async (_req, res, next) => {
        try {
            res.json(await db.listDevices());
        } catch (err) { next(err); }
    });

    app.get('/api/latest', async (req, res, next) => {
        try {
            const s = getState();
            const deviceId = Number(req.query.device) || s.deviceId;
            if (!deviceId) {
                res.json(null);
                return;
            }
            res.json(await db.latestSample(deviceId));
        } catch (err) { next(err); }
    });

    app.get('/api/measurements', async (req, res, next) => {
        try {
            const s = getState();
            const deviceId = Number(req.query.device) || s.deviceId;
            if (!deviceId) {
                res.json({ bucket: 'raw', points: [] });
                return;
            }
            const to = req.query.to ? new Date(req.query.to) : new Date();
            const from = req.query.from
                ? new Date(req.query.from)
                : new Date(to.getTime() - 24 * 3600 * 1000);
            if (Number.isNaN(from.getTime()) || Number.isNaN(to.getTime())) {
                res.status(400).json({ error: 'from/to must be ISO timestamps' });
                return;
            }
            const bucket = req.query.bucket || '5m';
            if (!Object.hasOwn(db.BUCKETS, bucket)) {
                res.status(400).json({ error: `bucket must be one of ${Object.keys(db.BUCKETS).join(', ')}` });
                return;
            }
            // Guard against a request that would pull the whole table.
            const maxPoints = clamp(Number(req.query.limit) || 2000, 1, 20000);
            const result = await db.series({ deviceId, from, to, bucket });
            res.json({
                ...result,
                points: result.points.slice(-maxPoints),
                from: from.toISOString(),
                to: to.toISOString(),
            });
        } catch (err) { next(err); }
    });

    app.get('/api/settings', async (req, res, next) => {
        try {
            const s = getState();
            const deviceId = Number(req.query.device) || s.deviceId;
            const stored = deviceId ? await db.getSettings(deviceId) : null;
            const live = s.deviceConfig
                ? pickSettings(s.deviceConfig)
                : stored ? pickSettings(stored) : null;
            res.json({
                // Live values win when the device is connected; the stored copy
                // lets the form render while the Pico is unplugged.
                source: s.deviceConfig ? 'device' : stored ? 'database' : 'default',
                settings: { ...DEFAULT_SETTINGS, ...(live ?? {}) },
            });
        } catch (err) { next(err); }
    });

    app.put('/api/settings', async (req, res, next) => {
        try {
            const { errors, settings } = validateSettings(req.body);
            if (errors.length) {
                res.status(400).json({ error: errors.join('; ') });
                return;
            }
            const s = getState();
            if (!link.connected) {
                res.status(503).json({ error: 'Pico is not connected; connect it to apply settings' });
                return;
            }
            link.apply(settings);
            link.requestConfig();
            if (s.deviceId) {
                await db.saveSettings(s.deviceId, settings);
            }
            res.json({ applied: settings });
        } catch (err) { next(err); }
    });

    // Asks the connected device for its settings; the reply arrives over the
    // WebSocket, so this only has to confirm the request went out.
    app.post('/api/settings/refresh', (_req, res) => {
        if (!link.connected) {
            res.status(503).json({ error: 'Pico is not connected' });
            return;
        }
        link.requestConfig();
        res.json({ requested: true });
    });

    // Serve the built Angular app when it exists, so a single process can be
    // installed as the service: plug in the Pico, open the dashboard.
    const uiDist = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', 'ui', 'dist', 'ui', 'browser');
    if (fs.existsSync(uiDist)) {
        app.use(express.static(uiDist));
        // Client-side routes fall back to index.html; /api is excluded so an
        // unknown API path still returns a JSON 404 rather than HTML.
        app.get(/^(?!\/api\/).*/, (_req, res) => res.sendFile(path.join(uiDist, 'index.html')));
    }

    app.use((err, _req, res, _next) => {
        console.error('[api]', err);
        res.status(500).json({ error: err.message });
    });

    return app;
}

export function attachWebSocket(server, getState) {
    const wss = new WebSocketServer({ server, path: '/ws' });
    wss.on('connection', (ws) => {
        ws.send(JSON.stringify({ type: 'hello', state: getState() }));
    });
    return {
        broadcast(type, payload) {
            const msg = JSON.stringify({ type, payload });
            for (const client of wss.clients) {
                if (client.readyState === 1) {
                    client.send(msg);
                }
            }
        },
        close: () => wss.close(),
    };
}
