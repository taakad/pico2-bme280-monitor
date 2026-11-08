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
// no chart dependency. It re-fetches whenever from/to/bucket/refreshKey change.
//
// The dashboard renders one chart per measurement, so the y axis is labelled
// with that metric's own scale: a single shared axis across °C / hPa / %RH is
// unreadable, which is exactly what the per-chart axes fix.
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
  private readonly height = 240;
  private readonly pad = { left: 58, right: 14, top: 14, bottom: 26 };

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

  // Value range across the chart's metrics (line + band), padded so the line
  // never touches the frame.
  private valueRange(): { lo: number; hi: number } | null {
    const pts = this.points();
    const values: number[] = [];
    for (const m of this.metrics()) {
      for (const p of pts) {
        const v = p[m.key];
        if (typeof v === 'number') values.push(v);
        if (m.minKey && typeof p[m.minKey] === 'number') values.push(p[m.minKey] as number);
        if (m.maxKey && typeof p[m.maxKey] === 'number') values.push(p[m.maxKey] as number);
      }
    }
    if (values.length === 0) {
      return null;
    }
    let lo = Math.min(...values);
    let hi = Math.max(...values);
    if (hi - lo < 1e-9) {
      // A flat series still needs a span, otherwise the scale divides by zero.
      const span = Math.max(Math.abs(lo) * 0.02, 1);
      lo -= span / 2;
      hi += span / 2;
    }
    const margin = (hi - lo) * 0.08;
    return { lo: lo - margin, hi: hi + margin };
  }

  // Rounded bounds so the axis labels are round numbers rather than the raw
  // min/max (e.g. 23.81..24.19 -> 23.75..24.25 with a 0.25 step).
  private niceRange(): { lo: number; hi: number; step: number } | null {
    const r = this.valueRange();
    if (!r) {
      return null;
    }
    const rawStep = (r.hi - r.lo) / 4;
    const magnitude = 10 ** Math.floor(Math.log10(rawStep));
    const step = [1, 2, 2.5, 5, 10]
      .map((f) => f * magnitude)
      .find((s) => s >= rawStep) ?? 10 * magnitude;
    const lo = Math.floor(r.lo / step) * step;
    const hi = Math.max(lo + step, Math.ceil(r.hi / step) * step);
    return { lo, hi, step };
  }

  private scale(): ((v: number) => number) | null {
    const r = this.niceRange();
    if (!r) {
      return null;
    }
    return (v: number) => this.pad.top + (1 - (v - r.lo) / (r.hi - r.lo)) * this.plotH;
  }

  linePath(metric: Metric): string {
    const y = this.scale();
    if (!y) {
      return '';
    }
    return this.points()
      .filter((p) => typeof p[metric.key] === 'number')
      .map((p, i) => `${i === 0 ? 'M' : 'L'} ${this.x(p.ts).toFixed(1)} ${y(p[metric.key] as number).toFixed(1)}`)
      .join(' ');
  }

  bandPath(metric: Metric): string {
    if (!metric.minKey || !metric.maxKey) {
      return '';
    }
    const y = this.scale();
    const pts = this.points().filter((p) => typeof p[metric.minKey!] === 'number' && typeof p[metric.maxKey!] === 'number');
    if (!y || pts.length < 2) {
      return '';
    }
    const up = pts.map((p, i) => `${i === 0 ? 'M' : 'L'} ${this.x(p.ts).toFixed(1)} ${y(p[metric.maxKey!] as number).toFixed(1)}`);
    const down = [...pts]
      .reverse()
      .map((p) => `L ${this.x(p.ts).toFixed(1)} ${y(p[metric.minKey!] as number).toFixed(1)}`);
    return `${up.join(' ')} ${down.join(' ')} Z`;
  }

  // Horizontal grid lines carrying the value they stand for. One metric per
  // chart means one scale, so the labels are unambiguous.
  readonly yTicks = computed(() => {
    const r = this.niceRange();
    if (!r) {
      return [];
    }
    const y = this.scale()!;
    const decimals = r.step >= 1 ? 0 : Math.min(3, Math.max(1, -Math.floor(Math.log10(r.step))));
    const ticks: { y: number; label: string }[] = [];
    for (let v = r.lo; v <= r.hi + r.step * 1e-6; v += r.step) {
      ticks.push({ y: y(v), label: v.toFixed(decimals) });
    }
    return ticks;
  });

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

  // Single-metric charts use the metric name + unit as the heading, which is
  // what tells the reader what the y axis means.
  readonly title = computed(() => {
    const ms = this.metrics();
    return ms.length === 1 ? `${ms[0].label} (${ms[0].unit})` : '測定値の推移';
  });

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
