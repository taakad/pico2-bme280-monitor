import { ChangeDetectionStrategy, Component, OnDestroy, OnInit, computed, inject, signal } from '@angular/core';
import { GaugeComponent } from '../gauge/gauge';
import { ApiService, Sample } from '../api.service';
import { HistoryChartComponent, Metric } from '../history/history-chart';

interface RangeOption {
  label: string;
  minutes: number;
  bucket: string;
}

@Component({
  selector: 'app-dashboard',
  imports: [GaugeComponent, HistoryChartComponent],
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './dashboard.html',
  styleUrl: './dashboard.scss',
})
export class DashboardComponent implements OnInit, OnDestroy {
  private readonly api = inject(ApiService);

  private readonly pollTimer: ReturnType<typeof setInterval>;

  readonly sample = computed(() => this.api.live());
  readonly link = computed(() => this.api.link());
  readonly config = computed(() => this.api.config());
  readonly wsOpen = computed(() => this.api.wsOpen());

  // Short windows use buckets fine enough to keep a readable number of points:
  // 1 分 at 1m buckets would be a single dot.
  readonly ranges: RangeOption[] = [
    { label: '1 分', minutes: 1, bucket: '5s' },
    { label: '5 分', minutes: 5, bucket: '15s' },
    { label: '30 分', minutes: 30, bucket: '1m' },
    { label: '1 時間', minutes: 60, bucket: '1m' },
    { label: '24 時間', minutes: 24 * 60, bucket: '5m' },
    { label: '7 日', minutes: 24 * 60 * 7, bucket: '1h' },
    { label: '30 日', minutes: 24 * 60 * 30, bucket: '6h' },
  ];
  readonly range = signal<RangeOption>(this.ranges[4]);

  // Bumped on a timer so the open charts pick up new buckets without a reload.
  readonly refreshKey = signal(0);

  readonly decimals = computed(() => this.config()?.decimals ?? 2);

  // One chart per measurement instead of three overlaid lines: each chart owns
  // its y axis, so the vertical scale is readable. The min/max keys draw the
  // intra-bucket spread as a band behind the line.
  readonly charts: Metric[] = [
    { key: 'temperature_c', minKey: 'temperature_c_min', maxKey: 'temperature_c_max', label: '温度', color: '#f2865f', unit: '°C' },
    { key: 'pressure_hpa', minKey: 'pressure_hpa_min', maxKey: 'pressure_hpa_max', label: '気圧', color: '#4f9cf9', unit: 'hPa' },
    { key: 'humidity_rh', minKey: 'humidity_rh_min', maxKey: 'humidity_rh_max', label: '相対湿度', color: '#48c9a9', unit: '%RH' },
    { key: 'dew_point_c', minKey: 'dew_point_c_min', maxKey: 'dew_point_c_max', label: '露点', color: '#9b8cf0', unit: '°C' },
    { key: 'absolute_humidity_gm3', minKey: 'absolute_humidity_gm3_min', maxKey: 'absolute_humidity_gm3_max', label: '絶対湿度', color: '#e0b64f', unit: 'g/m³' },
  ];

  // Gauge scales chosen for indoor monitoring; the component flags readings
  // that fall outside instead of pinning the needle.
  readonly temp = computed(() => this.sample()?.temperature_c ?? null);
  readonly pressure = computed(() => this.sample()?.pressure_hpa ?? null);
  readonly humidity = computed(() => this.sample()?.humidity_rh ?? null);
  readonly dew = computed(() => this.sample()?.dew_point_c ?? null);
  readonly absolute = computed(() => this.sample()?.absolute_humidity_gm3 ?? null);

  // The Angular template parser has no shift operator, so the filter label is
  // computed here.
  readonly filterLabel = computed(() => `x${1 << (this.config()?.filter ?? 0)}`);

  readonly updatedAt = computed(() => {
    const ts = this.sample()?.ts;
    return ts ? new Date(ts).toLocaleString() : '—';
  });

  readonly statusText = computed(() => {
    const l = this.link();
    if (!this.wsOpen()) {
      return 'サーバー接続なし';
    }
    switch (l.state) {
      case 'connected':
        return `Pico 接続中 (${l.port ?? ''})`;
      case 'waiting':
        return 'Pico を待ち受け中 — 接続すると自動で開始します';
      case 'rejected':
        return `設定が拒否されました: ${l.message ?? ''}`;
      case 'error':
        return `シリアルエラー: ${l.message ?? ''}`;
      default:
        return 'Pico 未接続';
    }
  });

  readonly statusClass = computed(() => {
    const l = this.link();
    if (!this.wsOpen()) return 'down';
    return l.state === 'connected' ? 'up' : l.state === 'rejected' || l.state === 'error' ? 'bad' : 'warn';
  });

  constructor() {
    // The WebSocket covers live data; this only catches samples that arrived
    // while the browser tab was closed.
    this.pollTimer = setInterval(() => {
      this.refreshKey.update((k) => k + 1);
      if (!this.api.wsOpen()) {
        void this.api.latest().then((s) => {
          if (s) this.api.live.set(s as Sample);
        });
      }
    }, 5000);
  }

  ngOnInit(): void {
    this.api.connectWebSocket();
    void this.api.latest().then((s) => {
      if (s && !this.api.live()) {
        this.api.live.set(s);
      }
    });
  }

  ngOnDestroy(): void {
    clearInterval(this.pollTimer);
  }

  setRange(option: RangeOption): void {
    this.range.set(option);
  }

  rangeStart(option: RangeOption): string {
    return new Date(Date.now() - option.minutes * 60 * 1000).toISOString();
  }

  rangeEnd(): string {
    return new Date().toISOString();
  }
}
