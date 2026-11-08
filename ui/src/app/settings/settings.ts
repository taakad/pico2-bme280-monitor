import { ChangeDetectionStrategy, Component, OnInit, computed, effect, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { ApiService, DeviceConfig } from '../api.service';

interface Option<T> {
  value: T;
  label: string;
}

@Component({
  selector: 'app-settings',
  changeDetection: ChangeDetectionStrategy.OnPush,
  imports: [FormsModule],
  templateUrl: './settings.html',
  styleUrl: './settings.scss',
})
export class SettingsComponent implements OnInit {
  private readonly api = inject(ApiService);

  readonly form = signal<DeviceConfig>({
    interval_ms: 2000,
    decimals: 2,
    average: 1,
    osr_t: 1,
    osr_p: 1,
    osr_h: 1,
    filter: 0,
    mode: 'normal',
  });

  readonly source = signal<'device' | 'database' | 'default'>('default');
  readonly dirty = signal(false);
  readonly saving = signal(false);
  readonly message = signal<{ kind: 'ok' | 'error' | 'info'; text: string } | null>(null);

  readonly link = computed(() => this.api.link());
  readonly connected = computed(() => this.api.connected());

  // Presets for the interval, plus a free numeric field for anything else.
  readonly intervalPresets: Option<number>[] = [
    { value: 10, label: '10 ms（最大速度）' },
    { value: 50, label: '50 ms' },
    { value: 500, label: '0.5 秒' },
    { value: 1000, label: '1 秒' },
    { value: 2000, label: '2 秒（既定）' },
    { value: 5000, label: '5 秒' },
    { value: 10000, label: '10 秒' },
    { value: 60000, label: '1 分' },
    { value: 300000, label: '5 分' },
    { value: 3600000, label: '1 時間' },
  ];

  readonly osrOptions: Option<number>[] = [1, 2, 4, 8, 16].map((v) => ({ value: v, label: `x${v}` }));

  readonly channels: { key: 'osr_t' | 'osr_p' | 'osr_h'; name: string }[] = [
    { key: 'osr_t', name: '温度' },
    { key: 'osr_p', name: '気圧' },
    { key: 'osr_h', name: '湿度' },
  ];
  readonly filterOptions: Option<number>[] = [
    { value: 0, label: 'オフ' },
    { value: 1, label: 'x2' },
    { value: 2, label: 'x4' },
    { value: 3, label: 'x8' },
    { value: 4, label: 'x16' },
  ];
  readonly averageOptions: Option<number>[] = [1, 2, 4, 8, 16, 32, 64].map((v) => ({ value: v, label: `${v} 件` }));
  readonly modeOptions: Option<'normal' | 'forced'>[] = [
    { value: 'normal', label: '連続 (normal)' },
    { value: 'forced', label: '単発 (forced)' },
  ];

  // Mirrors the firmware's own bounds so the form cannot submit a rejection.
  readonly limits = { intervalMin: 5, intervalMax: 3600000, decimalsMax: 6 };

  constructor() {
    // Keep the form in step with what the device reports, unless the user has
    // unsaved edits. effect() must be created in an injection context, so it
    // lives in the constructor rather than ngOnInit.
    effect(() => {
      const cfg = this.api.config();
      if (cfg && !this.dirty()) {
        this.form.update((f) => ({ ...f, ...cfg }));
        this.source.set('device');
      }
    });
  }

  ngOnInit(): void {
    this.api.connectWebSocket();
    void this.load();
  }

  async load(): Promise<void> {
    try {
      const res = await this.api.settings();
      this.form.set(res.settings);
      this.source.set(res.source as 'device' | 'database' | 'default');
      this.dirty.set(false);
    } catch (err) {
      this.message.set({ kind: 'error', text: `設定の読み込みに失敗: ${this.errText(err)}` });
    }
  }

  patch<K extends keyof DeviceConfig>(key: K, value: DeviceConfig[K]): void {
    this.form.update((f) => ({ ...f, [key]: value }));
    this.dirty.set(true);
    this.message.set(null);
  }

  async save(): Promise<void> {
    this.saving.set(true);
    this.message.set(null);
    try {
      const res = await this.api.saveSettings(this.form());
      this.form.set(res.applied);
      this.dirty.set(false);
      this.source.set('device');
      this.message.set({ kind: 'ok', text: 'デバイスに適用しました' });
    } catch (err) {
      this.message.set({ kind: 'error', text: this.errText(err) });
    } finally {
      this.saving.set(false);
    }
  }

  async refresh(): Promise<void> {
    await this.load();
    this.message.set({ kind: 'info', text: 'デバイスから読み直しました' });
  }

  resetDefaults(): void {
    this.form.set({
      interval_ms: 2000,
      decimals: 2,
      average: 1,
      osr_t: 1,
      osr_p: 1,
      osr_h: 1,
      filter: 0,
      mode: 'normal',
    });
    this.dirty.set(true);
  }

  // Estimated storage cost of the current cadence, so a 5 ms interval (about
  // 17 million rows/day) is a deliberate choice rather than an accident.
  readonly rowsPerDay = computed(() => Math.round(86400000 / this.form().interval_ms));

  private errText(err: unknown): string {
    return err instanceof Error ? err.message : String(err);
  }
}
