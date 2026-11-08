import pg from 'pg';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { settings } from './config.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const schemaPath = path.join(here, '..', '..', 'db', 'schema.sql');

const pool = new pg.Pool(settings.pg);

pool.on('error', (err) => {
    console.error('[db] idle client error:', err.message);
});

export async function connect() {
    const client = await pool.connect();
    client.release();
    return pool;
}

export async function migrate() {
    const sql = fs.readFileSync(schemaPath, 'utf8');
    await pool.query(sql);
    return schemaPath;
}

export async function upsertDevice(port) {
    const { rows } = await pool.query(
        `INSERT INTO device (port, last_seen)
         VALUES ($1, now())
         ON CONFLICT (port) DO UPDATE SET last_seen = now()
         RETURNING id, port, label, first_seen, last_seen`,
        [port],
    );
    return rows[0];
}

// Recomputes the hourly rollup for the buckets a batch touched, so long-range
// charts never have to scan the raw table.
async function refreshHourly(deviceId, minTs, maxTs) {
    await pool.query(
        `WITH touched AS (
             SELECT date_trunc('hour', ts) AS bucket
               FROM measurement
              WHERE device_id = $1 AND ts >= $2 AND ts <= $3
              GROUP BY 1
         )
         INSERT INTO measurement_hourly AS mh (
             device_id, bucket, n,
             temperature_c_avg, temperature_c_min, temperature_c_max,
             pressure_hpa_avg, pressure_hpa_min, pressure_hpa_max,
             humidity_rh_avg, humidity_rh_min, humidity_rh_max,
             dew_point_c_avg, dew_point_c_min, dew_point_c_max,
             absolute_humidity_gm3_avg, absolute_humidity_gm3_min, absolute_humidity_gm3_max)
         SELECT $1, t.bucket, COUNT(*),
                AVG(m.temperature_c), MIN(m.temperature_c), MAX(m.temperature_c),
                AVG(m.pressure_hpa), MIN(m.pressure_hpa), MAX(m.pressure_hpa),
                AVG(m.humidity_rh), MIN(m.humidity_rh), MAX(m.humidity_rh),
                AVG(m.dew_point_c), MIN(m.dew_point_c), MAX(m.dew_point_c),
                AVG(m.absolute_humidity_gm3), MIN(m.absolute_humidity_gm3), MAX(m.absolute_humidity_gm3)
           FROM touched t
           JOIN measurement m ON m.device_id = $1 AND date_trunc('hour', m.ts) = t.bucket
          GROUP BY t.bucket
         ON CONFLICT (device_id, bucket) DO UPDATE SET
             n = EXCLUDED.n,
             temperature_c_avg = EXCLUDED.temperature_c_avg,
             temperature_c_min = EXCLUDED.temperature_c_min,
             temperature_c_max = EXCLUDED.temperature_c_max,
             pressure_hpa_avg = EXCLUDED.pressure_hpa_avg,
             pressure_hpa_min = EXCLUDED.pressure_hpa_min,
             pressure_hpa_max = EXCLUDED.pressure_hpa_max,
             humidity_rh_avg = EXCLUDED.humidity_rh_avg,
             humidity_rh_min = EXCLUDED.humidity_rh_min,
             humidity_rh_max = EXCLUDED.humidity_rh_max,
             dew_point_c_avg = EXCLUDED.dew_point_c_avg,
             dew_point_c_min = EXCLUDED.dew_point_c_min,
             dew_point_c_max = EXCLUDED.dew_point_c_max,
             absolute_humidity_gm3_avg = EXCLUDED.absolute_humidity_gm3_avg,
             absolute_humidity_gm3_min = EXCLUDED.absolute_humidity_gm3_min,
             absolute_humidity_gm3_max = EXCLUDED.absolute_humidity_gm3_max`,
        [deviceId, minTs, maxTs],
    );
}

export async function insertMeasurements(deviceId, samples) {
    if (samples.length === 0) {
        return 0;
    }
    // config is stored per row rather than per batch: a graph can then explain
    // its own provenance even across a settings change mid-range.
    const columns = 12;
    const values = [];
    const params = [];
    samples.forEach((s, i) => {
        const b = i * columns;
        values.push(`(${Array.from({ length: columns }, (_, c) => `$${b + c + 1}`).join(',')})`);
        params.push(
            deviceId, s.ts, s.temperature_c, s.pressure_hpa, s.humidity_rh,
            s.dew_point_c, s.absolute_humidity_gm3, s.adc_t ?? null,
            s.adc_p ?? null, s.adc_h ?? null, s.samples ?? null,
            JSON.stringify(s.config ?? null),
        );
    });
    await pool.query(
        `INSERT INTO measurement (
             device_id, ts, temperature_c, pressure_hpa, humidity_rh,
             dew_point_c, absolute_humidity_gm3, adc_t, adc_p, adc_h, samples, config)
         VALUES ${values.join(',')}`,
        params,
    );
    await refreshHourly(deviceId, samples[0].ts, samples[samples.length - 1].ts);
    return samples.length;
}

export async function latestSample(deviceId) {
    const { rows } = await pool.query(
        `SELECT ts, temperature_c, pressure_hpa, humidity_rh, dew_point_c,
                absolute_humidity_gm3, adc_t, adc_p, adc_h, samples, config
           FROM measurement
          WHERE device_id = $1
          ORDER BY ts DESC
          LIMIT 1`,
        [deviceId],
    );
    return rows[0] ?? null;
}

export async function listDevices() {
    const { rows } = await pool.query(
        `SELECT d.id, d.port, d.label, d.first_seen, d.last_seen,
                (SELECT ts FROM measurement m WHERE m.device_id = d.id
                  ORDER BY ts DESC LIMIT 1) AS last_sample,
                (SELECT COUNT(*) FROM measurement m WHERE m.device_id = d.id)::int AS sample_count
           FROM device d
          ORDER BY d.id`,
    );
    return rows;
}

const BUCKETS = {
    raw: null,
    '5s': 5,
    '15s': 15,
    '1m': 60,
    '5m': 300,
    '15m': 900,
    '1h': 3600,
    '6h': 21600,
    '1d': 86400,
};

// Charts ask for a range plus a bucket size. Anything at or below one hour is
// served from the raw table; larger buckets come from the hourly rollup.
export async function series({ deviceId, from, to, bucket = '5m' }) {
    const seconds = BUCKETS[bucket];
    if (seconds === null) {
        const { rows } = await pool.query(
            `SELECT ts, temperature_c, pressure_hpa, humidity_rh,
                    dew_point_c, absolute_humidity_gm3
               FROM measurement
              WHERE device_id = $1 AND ts >= $2 AND ts <= $3
              ORDER BY ts`,
            [deviceId, from, to],
        );
        return { bucket, points: rows };
    }
    if (seconds % 3600 === 0) {
        const { rows } = await pool.query(
            `SELECT bucket AS ts,
                    temperature_c_avg AS temperature_c, temperature_c_min, temperature_c_max,
                    pressure_hpa_avg  AS pressure_hpa,  pressure_hpa_min,  pressure_hpa_max,
                    humidity_rh_avg   AS humidity_rh,   humidity_rh_min,   humidity_rh_max,
                    dew_point_c_avg   AS dew_point_c,   dew_point_c_min,   dew_point_c_max,
                    absolute_humidity_gm3_avg AS absolute_humidity_gm3,
                    absolute_humidity_gm3_min, absolute_humidity_gm3_max,
                    n
               FROM measurement_hourly
              WHERE device_id = $1 AND bucket >= $2 AND bucket <= $3
              ORDER BY bucket`,
            [deviceId, from, to],
        );
        return { bucket, points: rows };
    }
    const { rows } = await pool.query(
        `SELECT to_timestamp(floor(extract(epoch FROM ts) / $4) * $4) AS ts,
                AVG(temperature_c) AS temperature_c,
                MIN(temperature_c) AS temperature_c_min,
                MAX(temperature_c) AS temperature_c_max,
                AVG(pressure_hpa)  AS pressure_hpa,
                MIN(pressure_hpa)  AS pressure_hpa_min,
                MAX(pressure_hpa)  AS pressure_hpa_max,
                AVG(humidity_rh)   AS humidity_rh,
                MIN(humidity_rh)   AS humidity_rh_min,
                MAX(humidity_rh)   AS humidity_rh_max,
                AVG(dew_point_c)   AS dew_point_c,
                MIN(dew_point_c)   AS dew_point_c_min,
                MAX(dew_point_c)   AS dew_point_c_max,
                AVG(absolute_humidity_gm3) AS absolute_humidity_gm3,
                MIN(absolute_humidity_gm3) AS absolute_humidity_gm3_min,
                MAX(absolute_humidity_gm3) AS absolute_humidity_gm3_max,
                COUNT(*)::int AS n
           FROM measurement
          WHERE device_id = $1 AND ts >= $2 AND ts <= $3
          GROUP BY 1
          ORDER BY 1`,
        [deviceId, from, to, seconds],
    );
    return { bucket, points: rows };
}

export async function saveSettings(deviceId, cfg) {
    const { rows } = await pool.query(
        `INSERT INTO device_setting (device_id, interval_ms, decimals, average,
                                     osr_t, osr_p, osr_h, filter, mode, updated_at)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9, now())
         ON CONFLICT (device_id) DO UPDATE SET
             interval_ms = EXCLUDED.interval_ms,
             decimals = EXCLUDED.decimals,
             average = EXCLUDED.average,
             osr_t = EXCLUDED.osr_t,
             osr_p = EXCLUDED.osr_p,
             osr_h = EXCLUDED.osr_h,
             filter = EXCLUDED.filter,
             mode = EXCLUDED.mode,
             updated_at = now()
         RETURNING *`,
        [deviceId, cfg.interval_ms, cfg.decimals, cfg.average, cfg.osr_t,
         cfg.osr_p, cfg.osr_h, cfg.filter, cfg.mode],
    );
    return rows[0];
}

export async function getSettings(deviceId) {
    const { rows } = await pool.query('SELECT * FROM device_setting WHERE device_id = $1', [deviceId]);
    return rows[0] ?? null;
}

export async function close() {
    await pool.end();
}

export { BUCKETS };
