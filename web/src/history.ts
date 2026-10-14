import type { Frame } from './protocol';

const MAX_POINTS = 4000;

/**
 * Time series for the charts. Appends one sample per new engine frame; when
 * full, it halves its resolution (keeps every other sample) so the whole run
 * stays visible at bounded memory and redraw cost.
 */
export class History {
  t: number[] = [];
  K: number[] = [];
  U: number[] = [];
  E: number[] = [];
  rel: number[] = [];
  numX: (number | null)[] = [];
  refX: (number | null)[] = [];
  private lastSteps = -1;
  private stride = 1;
  private skipped = 0;

  clear() {
    this.t = []; this.K = []; this.U = []; this.E = []; this.rel = []; this.numX = []; this.refX = [];
    this.lastSteps = -1;
    this.stride = 1;
    this.skipped = 0;
  }

  push(f: Frame) {
    const d = f.diag;
    if (d.steps === this.lastSteps) return;
    if (d.steps < this.lastSteps) this.clear();
    this.lastSteps = d.steps;
    if (++this.skipped < this.stride && this.t.length > 0) return;
    this.skipped = 0;
    this.t.push(d.time);
    this.K.push(d.now.kinetic);
    this.U.push(d.now.potential);
    this.E.push(d.now.total);
    this.rel.push(d.relativeEnergyError);
    this.numX.push(f.reference ? f.reference.numericX : null);
    this.refX.push(f.reference ? f.reference.x : null);
    if (this.t.length > MAX_POINTS) {
      const keep = <T,>(a: T[]) => a.filter((_, i) => i % 2 === 0);
      this.t = keep(this.t); this.K = keep(this.K); this.U = keep(this.U); this.E = keep(this.E);
      this.rel = keep(this.rel); this.numX = keep(this.numX); this.refX = keep(this.refX);
      this.stride *= 2;
    }
  }
}
