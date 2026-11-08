import { ChangeDetectionStrategy, Component, computed, input } from '@angular/core';

// A 270-degree arc gauge drawn by hand: no chart library, so the dashboard
// stays dependency-free and the markup is inspectable.
@Component({
  selector: 'app-gauge',
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './gauge.html',
  styleUrl: './gauge.scss',
})
export class GaugeComponent {
  readonly label = input.required<string>();
  readonly value = input<number | null>(null);
  readonly unit = input.required<string>();
  readonly min = input.required<number>();
  readonly max = input.required<number>();
  readonly decimals = input(1);
  readonly accent = input('#4f9cf9');

  // Arc spans 270 degrees, opening downward: 135deg -> 405deg in SVG space.
  private readonly startDeg = 135;
  private readonly sweep = 270;
  private readonly radius = 70;
  private readonly cx = 100;
  private readonly cy = 104;

  private polar(angleDeg: number, radius = this.radius): [number, number] {
    const rad = (angleDeg * Math.PI) / 180;
    return [this.cx + radius * Math.cos(rad), this.cy + radius * Math.sin(rad)];
  }

  private arc(fromDeg: number, toDeg: number, radius = this.radius): string {
    const [x1, y1] = this.polar(fromDeg, radius);
    const [x2, y2] = this.polar(toDeg, radius);
    const large = toDeg - fromDeg > 180 ? 1 : 0;
    return `M ${x1.toFixed(2)} ${y1.toFixed(2)} A ${radius} ${radius} 0 ${large} 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`;
  }

  readonly trackPath = this.arc(this.startDeg, this.startDeg + this.sweep);

  readonly fraction = computed(() => {
    const v = this.value();
    if (v === null || !Number.isFinite(v)) {
      return 0;
    }
    const span = this.max() - this.min();
    return Math.min(1, Math.max(0, (v - this.min()) / span));
  });

  readonly valuePath = computed(() => {
    const f = this.fraction();
    if (f <= 0.001) {
      return '';
    }
    return this.arc(this.startDeg, this.startDeg + this.sweep * f);
  });

  readonly needlePath = computed(() => {
    const angle = this.startDeg + this.sweep * this.fraction();
    const [x1, y1] = this.polar(angle, this.radius - 46);
    const [x2, y2] = this.polar(angle, this.radius - 8);
    return `M ${x1.toFixed(2)} ${y1.toFixed(2)} L ${x2.toFixed(2)} ${y2.toFixed(2)}`;
  });

  readonly ticks = computed(() =>
    [0, 0.25, 0.5, 0.75, 1].map((f) => {
      const angle = this.startDeg + this.sweep * f;
      const [x1, y1] = this.polar(angle, this.radius + 3);
      const [x2, y2] = this.polar(angle, this.radius + 9);
      const [lx, ly] = this.polar(angle, this.radius + 19);
      const label = (this.min() + (this.max() - this.min()) * f).toFixed(0);
      return { d: `M ${x1.toFixed(2)} ${y1.toFixed(2)} L ${x2.toFixed(2)} ${y2.toFixed(2)}`, lx, ly, label };
    }),
  );

  readonly display = computed(() => {
    const v = this.value();
    return v === null || !Number.isFinite(v) ? '--' : v.toFixed(this.decimals());
  });

  // Warn when the reading sits outside the gauge scale rather than pinning it.
  readonly outOfRange = computed(() => {
    const v = this.value();
    return v !== null && Number.isFinite(v) && (v < this.min() || v > this.max());
  });
}
