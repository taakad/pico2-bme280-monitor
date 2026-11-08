-- pico2-bme280-monitor schema
--
-- One row per sample; one row per device; the last-known settings are kept
-- so the settings screen can show them while the Pico is unplugged.
--
-- Apply with:
--   psql -h /tmp -p 5433 -U pico -d pico_monitor -f db/schema.sql

CREATE TABLE IF NOT EXISTS device (
    id           SERIAL PRIMARY KEY,
    -- Stable identifier for the monitor: the serial port path the collector
    -- saw it on (e.g. /dev/cu.usbmodem1101). Good enough for a single-board
    -- setup and survives reboots.
    port         TEXT NOT NULL UNIQUE,
    label        TEXT,
    first_seen   TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_seen    TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS measurement (
    id                      BIGSERIAL PRIMARY KEY,
    device_id               INT NOT NULL REFERENCES device(id) ON DELETE CASCADE,
    -- Host clock at collection time. The Pico has no RTC, so the host is the
    -- only trustworthy time source.
    ts                      TIMESTAMPTZ NOT NULL,
    temperature_c           REAL NOT NULL,
    pressure_hpa            REAL NOT NULL,
    humidity_rh             REAL NOT NULL,
    dew_point_c             REAL NOT NULL,
    absolute_humidity_gm3   REAL NOT NULL,
    adc_t                   INT,
    adc_p                   INT,
    adc_h                   INT,
    -- How many raw readings the firmware averaged into this sample.
    samples                 SMALLINT,
    -- Settings snapshot at collection time, so a graph can explain its own
    -- provenance (e.g. a step caused by switching oversampling).
    config                  JSONB
);

-- The chart queries are always "one device, one time range".
CREATE INDEX IF NOT EXISTS measurement_device_ts
    ON measurement (device_id, ts DESC);

-- Last-known firmware settings per device (mirrors the firmware `get` reply).
CREATE TABLE IF NOT EXISTS device_setting (
    device_id     INT PRIMARY KEY REFERENCES device(id) ON DELETE CASCADE,
    interval_ms   INT NOT NULL,
    decimals      SMALLINT NOT NULL,
    average       SMALLINT NOT NULL,
    osr_t         SMALLINT NOT NULL,
    osr_p         SMALLINT NOT NULL,
    osr_h         SMALLINT NOT NULL,
    filter        SMALLINT NOT NULL,
    mode          TEXT NOT NULL CHECK (mode IN ('normal', 'forced')),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Hourly rollup so a multi-month range does not scan every raw row.
-- The collector refreshes it after each insert batch.
CREATE TABLE IF NOT EXISTS measurement_hourly (
    device_id               INT NOT NULL REFERENCES device(id) ON DELETE CASCADE,
    bucket                  TIMESTAMPTZ NOT NULL,
    n                       INT NOT NULL,
    temperature_c_avg       REAL,
    temperature_c_min       REAL,
    temperature_c_max       REAL,
    pressure_hpa_avg        REAL,
    pressure_hpa_min        REAL,
    pressure_hpa_max        REAL,
    humidity_rh_avg         REAL,
    humidity_rh_min         REAL,
    humidity_rh_max         REAL,
    dew_point_c_avg         REAL,
    dew_point_c_min         REAL,
    dew_point_c_max         REAL,
    absolute_humidity_gm3_avg REAL,
    absolute_humidity_gm3_min REAL,
    absolute_humidity_gm3_max REAL,
    PRIMARY KEY (device_id, bucket)
);

-- Idempotent upgrades for databases created before a column existed.
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS dew_point_c_avg REAL;
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS dew_point_c_min REAL;
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS dew_point_c_max REAL;
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS absolute_humidity_gm3_avg REAL;
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS absolute_humidity_gm3_min REAL;
ALTER TABLE measurement_hourly ADD COLUMN IF NOT EXISTS absolute_humidity_gm3_max REAL;

-- Backfill rollup rows written before those columns existed, so long-range
-- charts of dew point / absolute humidity are not blank for old data. Only
-- rows with a NULL average are touched, so this is a no-op once caught up.
UPDATE measurement_hourly mh
   SET dew_point_c_avg = a.dew_avg,
       dew_point_c_min = a.dew_min,
       dew_point_c_max = a.dew_max,
       absolute_humidity_gm3_avg = a.abs_avg,
       absolute_humidity_gm3_min = a.abs_min,
       absolute_humidity_gm3_max = a.abs_max
  FROM (
        SELECT device_id,
               date_trunc('hour', ts) AS bucket,
               AVG(dew_point_c)            AS dew_avg,
               MIN(dew_point_c)            AS dew_min,
               MAX(dew_point_c)            AS dew_max,
               AVG(absolute_humidity_gm3)  AS abs_avg,
               MIN(absolute_humidity_gm3)  AS abs_min,
               MAX(absolute_humidity_gm3)  AS abs_max
          FROM measurement
         GROUP BY 1, 2
  ) a
 WHERE mh.device_id = a.device_id
   AND mh.bucket = a.bucket
   AND mh.dew_point_c_avg IS NULL;
