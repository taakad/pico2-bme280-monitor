import { Injectable, signal, computed } from '@angular/core';

export interface Sample {
  ts: string;
  temperature_c: number;
  pressure_hpa: number;
  humidity_rh: number;
  dew_point_c: number;
  absolute_humidity_gm3: number;
  adc_t?: number;
  adc_p?: number;
  adc_h?: number;
  samples?: number;
  config?: Record<string, unknown>;
}

export interface DeviceConfig {
  interval_ms: number;
  decimals: number;
  average: number;
  osr_t: number;
  osr_p: number;
  osr_h: number;
  filter: number;
  mode: 'normal' | 'forced';
}

export interface SeriesPoint {
  ts: string;
  temperature_c: number;
  pressure_hpa: number;
  humidity_rh: number;
  temperature_c_min?: number;
  temperature_c_max?: number;
  pressure_hpa_min?: number;
  pressure_hpa_max?: number;
  humidity_rh_min?: number;
  humidity_rh_max?: number;
  n?: number;
}

export interface Series {
  bucket: string;
  from: string;
  to: string;
  points: SeriesPoint[];
}

export type LinkState = 'starting' | 'connected' | 'disconnected' | 'waiting' | 'error' | 'rejected';

// Talks to the collector service. REST for history and settings, WebSocket for
// the live sample stream that drives the gauges.
@Injectable({ providedIn: 'root' })
export class ApiService {
  private readonly base = '';

  readonly live = signal<Sample | null>(null);
  readonly config = signal<DeviceConfig | null>(null);
  readonly link = signal<{ state: LinkState; port: string | null; message?: string }>({
    state: 'starting',
    port: null,
  });
  readonly wsOpen = signal(false);
  readonly sampleCount = signal(0);

  readonly connected = computed(() => this.link().state === 'connected');

  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  connectWebSocket(): void {
    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
      return;
    }
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${proto}//${location.host}/ws`);
    this.ws = ws;

    ws.onopen = () => this.wsOpen.set(true);
    ws.onclose = () => {
      this.wsOpen.set(false);
      this.ws = null;
      // Back off so a restarting collector is not hammered.
      this.reconnectTimer = setTimeout(() => this.connectWebSocket(), 2000);
    };
    ws.onerror = () => ws.close();
    ws.onmessage = (ev) => {
      let msg: { type: string; payload: unknown; state?: unknown };
      try {
        msg = JSON.parse(ev.data as string);
      } catch {
        return;
      }
      switch (msg.type) {
        case 'sample':
          this.live.set(msg.payload as Sample);
          this.sampleCount.update((n) => n + 1);
          break;
        case 'config':
          this.config.set(msg.payload as DeviceConfig);
          break;
        case 'status':
          this.link.set(msg.payload as { state: LinkState; port: string | null; message?: string });
          break;
        case 'hello': {
          const state = msg.state as { serial?: { state: LinkState; port: string | null }; deviceConfig?: DeviceConfig };
          if (state?.serial) {
            this.link.set(state.serial);
          }
          if (state?.deviceConfig) {
            this.config.set(state.deviceConfig);
          }
          break;
        }
        default:
          break;
      }
    };
  }

  private async request<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(this.base + path, {
      headers: { 'content-type': 'application/json' },
      ...init,
    });
    const body = await res.json().catch(() => null);
    if (!res.ok) {
      const message = (body && (body.error as string)) || `HTTP ${res.status}`;
      throw new Error(message);
    }
    return body as T;
  }

  latest(): Promise<Sample | null> {
    return this.request<Sample | null>('/api/latest');
  }

  series(range: { from?: string; to?: string; bucket?: string }): Promise<Series> {
    const params = new URLSearchParams();
    if (range.from) params.set('from', range.from);
    if (range.to) params.set('to', range.to);
    if (range.bucket) params.set('bucket', range.bucket);
    return this.request<Series>(`/api/measurements?${params}`);
  }

  settings(): Promise<{ source: string; settings: DeviceConfig }> {
    return this.request('/api/settings');
  }

  // Asks the device itself; the fresh values arrive on the config signal.
  refreshFromDevice(): Promise<{ requested: boolean }> {
    return this.request('/api/settings/refresh', { method: 'POST' });
  }

  saveSettings(cfg: DeviceConfig): Promise<{ applied: DeviceConfig }> {
    return this.request('/api/settings', { method: 'PUT', body: JSON.stringify(cfg) });
  }

  devices(): Promise<unknown[]> {
    return this.request<unknown[]>('/api/devices');
  }
}
