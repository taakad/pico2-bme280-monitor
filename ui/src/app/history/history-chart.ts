import { ChangeDetectionStrategy, Component, computed, effect, inject, input, signal } from '@angular/core';
import { ApiService, Series, SeriesPoint } from '../api.service';

export interface Metric {
  key: keyof SeriesPoint;
  minKey?: keyof SeriesPoint;
  maxKey?: keyof SeriesPoint;
  label: string;
  color: string;
  unit: string;
}

// SVG line chart with an optional min/max band, drawn by hand so the app has
// no chart dependency. It re-fetches whenever from/to/bucket change.
@Component({
  selector: 'app-history-chart',
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './history-chart.html',
  styleUrl: './history-chart.scss',
})
export class HistoryChartComponent {
  private readonly api = inject(ApiService);

  readonly from = input.required<string>();
  readonly to = input.required<string>();
  readonly bucket = input.required<string>();
  readonly metrics = input<Metric[]>([]);
  readonly refreshKey = input(0);

  readonly series = signal<Series | null>(null);
  readonly error = signal<string | null>(null);
  readonly loading = signal(false);

  private readonly width = 900;
  private readonly height = 260;
  private readonly pad = { left: 46, right: 14, top: 14, bottom: 26 };

  constructor() {
    effect(() => {
      const from = this.from();
      const to = this.to();
      const bucket = this.bucket();
      this.refreshKey();
      void this.load(from, to, bucket);
    });
  }

  private async load(from: string, to: string, bucket: string): Promise<void> {
    this.loading.set(true);
    try {
      this.series.set(await this.api.series({ from, to, bucket }));
      this.error.set(null);
    } catch (err) {
      this.error.set(err instanceof Error ? err.message : String(err));
    } finally {
      this.loading.set(false);
    }
  }

  private get plotW(): number {
    return this.width - this.pad.left - this.pad.right;
  }

  private get plotH(): number {
    return this.height - this.pad.top - this.pad.bottom;
  }

  readonly points = computed(() => this.series()?.points ?? []);

  private timeRange(): [number, number] {
    const pts = this.points();
    if (pts.length === 0) {
      const from = Date.parse(this.from());
      return [from, Date.parse(this.to())];
    }
    const first = Date.parse(pts[0].ts);
    const last = Date.parse(pts[pts.length - 1].ts);
    // A single point still needs a span, otherwise the x scale divides by zero.
    return last > first ? [first, last] : [first - 60000, last + 60000];
  }

  private x(ts: string): number {
    const [t0, t1] = this.timeRange();
    return this.pad.left + ((Date.parse(ts) - t0) / (t1 - t0)) * this.plotW;
  }

  // Each metric gets its own y scale; the chart overlays them, which is what a
  // temperature/pressure/humidity comparison needs.
  private scaleFor(metric: Metric): (v: number) => number {
    const pts = this.points();
    const values: number[] = [];
    for (const p of pts) {
      const v = p[metric.key];
      if (typeof v === 'number') values.push(v);
      if (metric.minKey && typeof p[metric.minKey] === 'number') values.push(p[metric.minKey] as number);
      if (metric.maxKey && typeof p[metric.maxKey] === 'number') values.push(p[metric.maxKey] as number);
    }
    if (values.length === 0) {
      return () => this.pad.top + this.plotH / 2;
    }
    let lo = Math.min(...values);
    let hi = Math.max(...values);
    if (hi - lo < 1e-9) {
      lo -= 1;
      hi += 1;
    }
    const margin = (hi - lo) * 0.08;
    lo -= margin;
    hi += margin;
    return (v: number) => this.pad.top + (1 - (v - lo) / (hi - lo)) * this.plotH;
  }

  linePath(metric: Metric): string {
    const y = this.scaleFor(metric);
    return this.points()
      .map((p, i) => `${i === 0 ? 'M' : 'L'} ${this.x(p.ts).toFixed(1)} ${y(p[metric.key] as number).toFixed(1)}`)
      .join(' ');
  }

  bandPath(metric: Metric): string {
    if (!metric.minKey || !metric.maxKey) {
      return '';
    }
    const pts = this.points().filter((p) => typeof p[metric.minKey!] === 'number' && typeof p[metric.maxKey!] === 'number');
    if (pts.length < 2) {
      return '';
    }
    const y = this.scaleFor(metric);
    const up = pts.map((p, i) => `${i === 0 ? 'M' : 'L'} ${this.x(p.ts).toFixed(1)} ${y(p[metric.maxKey!] as number).toFixed(1)}`);
    const down = [...pts]
      .reverse()
      .map((p) => `L ${this.x(p.ts).toFixed(1)} ${y(p[metric.minKey!] as number).toFixed(1)}`);
    return `${up.join(' ')} ${down.join(' ')} Z`;
  }

  // Horizontal rules carry no y labels: each metric has its own scale, so the
  // legend and the footer readouts are what give the lines meaning.
  readonly gridLines = computed(() =>
    [0, 0.25, 0.5, 0.75, 1].map((f) => this.pad.top + f * this.plotH),
  );

  readonly timeTicks = computed(() => {
    const [t0, t1] = this.timeRange();
    return [0, 0.25, 0.5, 0.75, 1].map((f) => {
      const x = this.pad.left + f * this.plotW;
      const t = new Date(t0 + f * (t1 - t0));
      return { x, label: this.timeLabel(t, t1 - t0) };
    });
  });

  private timeLabel(d: Date, spanMs: number): string {
    if (spanMs > 3 * 24 * 3600 * 1000) {
      return `${d.getMonth() + 1}/${d.getDate()}`;
    }
    return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  }

  readonly lastValue = computed(() => {
    const pts = this.points();
    if (pts.length === 0) {
      return [];
    }
    const last = pts[pts.length - 1];
    return this.metrics().map((m) => ({
      label: m.label,
      color: m.color,
      value: typeof last[m.key] === 'number' ? (last[m.key] as number).toFixed(2) : '—',
      unit: m.unit,
    }));
  });

  readonly sampleCount = computed(() =>
    this.points().reduce((sum, p) => sum + (p.n ?? 1), 0),
  );
}
