import test from 'node:test';
import assert from 'node:assert/strict';
import * as db from '../src/db.js';

// Uses the local development database; skipped when it is not reachable so CI
// without Postgres still passes.
let available = true;
try {
    await db.connect();
} catch {
    available = false;
}

test('measurement round trip and hourly rollup', { skip: !available }, async (t) => {
    const port = `/test/serial-${Date.now()}`;
    const device = await db.upsertDevice(port);
    t.after(async () => {
        await db.close();
    });

    const base = new Date(Date.now() - 30 * 60 * 1000);
    const samples = Array.from({ length: 12 }, (_, i) => ({
        ts: new Date(base.getTime() + i * 120000),
        temperature_c: 20 + i * 0.1,
        pressure_hpa: 1010 + i * 0.05,
        humidity_rh: 50 + i,
        dew_point_c: 12,
        absolute_humidity_gm3: 10,
        adc_t: 530000 + i,
        adc_p: 270000,
        adc_h: 28000,
        samples: 4,
        config: { interval_ms: 2000, decimals: 2 },
    }));

    assert.equal(await db.insertMeasurements(device.id, samples), 12);

    const latest = await db.latestSample(device.id);
    assert.equal(latest.temperature_c, 21.1);
    assert.equal(latest.config.interval_ms, 2000);

    const raw = await db.series({ deviceId: device.id, from: base, to: new Date(), bucket: 'raw' });
    assert.equal(raw.points.length, 12);

    const bucketed = await db.series({ deviceId: device.id, from: base, to: new Date(), bucket: '5m' });
    assert.ok(bucketed.points.length >= 1);
    assert.ok(bucketed.points[0].n >= 1);
    assert.ok(typeof bucketed.points[0].temperature_c_min === 'number');

    // The rollup row is keyed by the top of the hour, which can sit just
    // before `base` depending on when the test runs, so widen the range.
    const hourly = await db.series({
        deviceId: device.id,
        from: new Date(base.getTime() - 3600 * 1000),
        to: new Date(),
        bucket: '1h',
    });
    assert.ok(hourly.points.length >= 1);

    const settings = await db.saveSettings(device.id, {
        interval_ms: 1000, decimals: 3, average: 8, osr_t: 4, osr_p: 4, osr_h: 4, filter: 2, mode: 'normal',
    });
    assert.equal(settings.interval_ms, 1000);
    assert.deepEqual((await db.getSettings(device.id)).mode, 'normal');

    const devices = await db.listDevices();
    assert.ok(devices.some((d) => d.id === device.id && d.sample_count === 12));
});
