// Environment-driven configuration. Defaults match the local development
// setup: PostgreSQL from Postgres.app copied under ~/toolchain, and the Pico
// auto-detected by USB vendor id.
const int = (value, fallback) => {
    const parsed = Number.parseInt(value ?? '', 10);
    return Number.isFinite(parsed) ? parsed : fallback;
};

export const settings = {
    pg: {
        host: process.env.PGHOST || '/tmp',
        port: int(process.env.PGPORT, 5433),
        database: process.env.PGDATABASE || 'pico_monitor',
        user: process.env.PGUSER || 'pico',
        password: process.env.PGPASSWORD || '',
    },
    http: {
        port: int(process.env.PORT, 8787),
        host: process.env.HOST || '127.0.0.1',
    },
    serial: {
        // Empty means auto-detect by USB vendor id.
        port: process.env.PICO_PORT || '',
        baud: int(process.env.PICO_BAUD, 115200),
        retryMs: int(process.env.PICO_RETRY_MS, 2000),
    },
    // Raspberry Pi vendor id; 0x0D28 is the BOOTSEL mass-storage interface.
    vendorIds: ['2E8A', '0D28'],
};
