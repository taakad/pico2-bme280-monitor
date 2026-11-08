import test from 'node:test';
import assert from 'node:assert/strict';
import { parseConfigLine } from '../src/serial.js';
import { validateSettings, DEFAULT_SETTINGS } from '../src/api.js';

test('parseConfigLine reads the firmware get reply', () => {
    const cfg = parseConfigLine(
        'interval=1000 decimals=4 average=8 osr.t=4 osr.p=2 osr.h=1 filter=2 mode=forced json=true');
    assert.deepEqual(cfg, {
        interval_ms: 1000, decimals: 4, average: 8, osr_t: 4, osr_p: 2, osr_h: 1,
        filter: 2, mode: 'forced', json: true,
    });
});

test('parseConfigLine ignores unknown tokens', () => {
    const cfg = parseConfigLine('interval=500 future=thing');
    assert.equal(cfg.interval_ms, 500);
    assert.equal(cfg.future, undefined);
});

test('validateSettings accepts a full valid payload', () => {
    const { errors, settings } = validateSettings({
        interval_ms: 500, decimals: 3, average: 16, osr_t: 8, osr_p: 4, osr_h: 2,
        filter: 3, mode: 'forced',
    });
    assert.deepEqual(errors, []);
    assert.equal(settings.interval_ms, 500);
    assert.equal(settings.mode, 'forced');
});

test('validateSettings rejects out-of-range values', () => {
    const cases = [
        { interval_ms: 50 },
        { interval_ms: 4000000 },
        { decimals: 9 },
        { average: 0 },
        { average: 65 },
        { osr_t: 3 },
        { filter: 5 },
        { mode: 'sleep' },
        { decimals: 1.5 },
    ];
    for (const body of cases) {
        const { errors } = validateSettings(body);
        assert.ok(errors.length === 1, `${JSON.stringify(body)} should be rejected`);
    }
});

test('validateSettings fills missing keys from defaults', () => {
    const { errors, settings } = validateSettings({ decimals: 4 });
    assert.deepEqual(errors, []);
    assert.deepEqual(settings, { ...DEFAULT_SETTINGS, decimals: 4 });
});
