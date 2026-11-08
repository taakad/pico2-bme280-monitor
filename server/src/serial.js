import { SerialPort } from 'serialport';
import { settings } from './config.js';

// Talks to the firmware's serial console: enables JSON sample framing, then
// emits parsed samples and command acknowledgements. Reconnects on unplug so
// the "plug in and measure" service mode works without restarting anything.
export class MonitorLink extends EventTarget {
    #port = null;
    #closed = false;
    #retryTimer = null;
    #pending = [];
    #buffer = '';
    #portPath = '';
    #lastConfig = null;

    // Events: 'sample' (CustomEvent, detail = sample), 'status' (detail =
    // {state, port, message}), 'config' (detail = settings snapshot).
    constructor() {
        super();
    }

    get portPath() {
        return this.#portPath;
    }

    get connected() {
        return this.#port !== null && this.#port.isOpen;
    }

    start() {
        this.#closed = false;
        this.#open();
    }

    stop() {
        this.#closed = true;
        if (this.#retryTimer) {
            clearTimeout(this.#retryTimer);
            this.#retryTimer = null;
        }
        this.#detach();
    }

    // Queues `set k=v` lines; they are written as soon as the port is open.
    apply(config) {
        this.#lastConfig = { ...config };
        this.#queue(config);
        if (this.connected) {
            this.#flush();
        }
    }

    // Asks the device for its current settings; the reply arrives as a 'config'
    // event because the firmware prints the snapshot after every command.
    requestConfig() {
        this.#writeLine('get');
    }

    #queue(config) {
        this.#pending = [
            'set json=true',
            `set interval=${config.interval_ms}`,
            `set decimals=${config.decimals}`,
            `set average=${config.average}`,
            `set osr.t=${config.osr_t}`,
            `set osr.p=${config.osr_p}`,
            `set osr.h=${config.osr_h}`,
            `set filter=${config.filter}`,
            `set mode=${config.mode}`,
        ];
    }

    #flush() {
        const lines = this.#pending;
        this.#pending = [];
        for (const line of lines) {
            this.#writeLine(line);
        }
    }

    #writeLine(line) {
        if (!this.connected) {
            return;
        }
        this.#port.write(line + '\n');
    }

    async #detect() {
        if (settings.serial.port) {
            return settings.serial.port;
        }
        const ports = await SerialPort.list();
        const live = ports.filter((p) =>
            settings.vendorIds.some((v) => (p.vendorId || '').toUpperCase() === v));
        // Prefer a CDC serial interface (RP2E8A) over the BOOTSEL drive.
        const serial = live.filter((p) => p.pid && p.pid.toUpperCase() !== '0003');
        const pick = serial[0] || live[0] || ports.find((p) => /usbmodem|ttyACM|ttyUSB/i.test(p.path));
        if (!pick) {
            return null;
        }
        // On macOS the cu.* dial-out device never blocks waiting for DTR,
        // unlike tty.*; prefer it when both exist.
        if (pick.path.startsWith('/dev/tty.')) {
            const cu = pick.path.replace('/dev/tty.', '/dev/cu.');
            if (ports.some((p) => p.path === cu)) {
                return cu;
            }
        }
        return pick.path;
    }

    async #open() {
        if (this.#closed) {
            return;
        }
        const path = await this.#detect();
        if (!path) {
            this.#emit('status', { state: 'waiting', port: null, message: 'no Pico detected' });
            this.#retryTimer = setTimeout(() => this.#open(), settings.serial.retryMs);
            return;
        }
        this.#portPath = path;
        const port = new SerialPort({ path, baudRate: settings.serial.baud, autoOpen: false });
        this.#port = port;
        this.#buffer = '';

        port.on('data', (chunk) => this.#onData(chunk));
        port.on('error', (err) => {
            this.#emit('status', { state: 'error', port: path, message: err.message });
        });
        port.on('close', () => {
            this.#emit('status', { state: 'disconnected', port: path, message: 'serial closed' });
            if (!this.#closed) {
                this.#retryTimer = setTimeout(() => this.#open(), settings.serial.retryMs);
            }
        });

        try {
            await new Promise((resolve, reject) => {
                port.open((err) => (err ? reject(err) : resolve()));
            });
        } catch (err) {
            this.#emit('status', { state: 'error', port: path, message: err.message });
            this.#detach();
            this.#retryTimer = setTimeout(() => this.#open(), settings.serial.retryMs);
            return;
        }

        this.#emit('status', { state: 'connected', port: path, message: 'serial open' });
        // Re-assert JSON framing and the last known settings after a reconnect.
        if (this.#lastConfig) {
            this.#queue(this.#lastConfig);
        } else {
            this.#pending = ['set json=true'];
        }
        this.#flush();
    }

    #detach() {
        const port = this.#port;
        this.#port = null;
        if (port && port.isOpen) {
            port.removeAllListeners('data');
            port.removeAllListeners('error');
            port.removeAllListeners('close');
            port.close(() => {});
        }
    }

    #onData(chunk) {
        this.#buffer += chunk.toString('utf8');
        // The firmware terminates with CRLF; tolerate a bare LF too.
        let idx;
        while ((idx = this.#buffer.indexOf('\n')) >= 0) {
            const line = this.#buffer.slice(0, idx).replace(/\r$/, '').trim();
            this.#buffer = this.#buffer.slice(idx + 1);
            if (line.length > 0) {
                this.#onLine(line);
            }
        }
        if (this.#buffer.length > 4096) {
            this.#buffer = '';   // never let a non-text stream grow unbounded
        }
    }

    #onLine(line) {
        if (line.startsWith('{')) {
            try {
                const sample = JSON.parse(line);
                this.#emit('sample', sample);
            } catch {
                /* partial or corrupt frame: the next line resynchronises */
            }
            return;
        }
        if (line.startsWith('interval=')) {
            this.#emit('config', parseConfigLine(line));
            return;
        }
        if (line.startsWith('error')) {
            this.#emit('status', { state: 'rejected', port: this.#portPath, message: line });
        }
    }

    #emit(type, detail) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }
}

// "interval=2000 decimals=2 average=1 osr.t=1 osr.p=1 osr.h=1 filter=0 mode=normal json=true"
export function parseConfigLine(line) {
    const cfg = {};
    for (const token of line.split(/\s+/)) {
        const [k, v] = token.split('=');
        if (v === undefined) {
            continue;
        }
        switch (k) {
            case 'interval': cfg.interval_ms = Number(v); break;
            case 'decimals': cfg.decimals = Number(v); break;
            case 'average': cfg.average = Number(v); break;
            case 'osr.t': cfg.osr_t = Number(v); break;
            case 'osr.p': cfg.osr_p = Number(v); break;
            case 'osr.h': cfg.osr_h = Number(v); break;
            case 'filter': cfg.filter = Number(v); break;
            case 'mode': cfg.mode = v; break;
            case 'json': cfg.json = v === 'true'; break;
            default: break;
        }
    }
    return cfg;
}
