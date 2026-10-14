import { useEffect, useRef } from 'react';
import type { Frame, Topology } from './protocol';

// Drawing colours, kept in sync with the CSS tokens in styles.css.
const C = {
  sheet: '#F9FBFC',
  gridMinor: '#E6ECF2',
  gridMajor: '#BFCCD8',
  label: '#7A8591',
  ink: '#1B2430',
  blue: '#1F4FD8',
  blueFill: 'rgba(31, 79, 216, 0.16)',
  red: '#D23A4B',
};

const TRAIL = 900; // points kept per traced body

interface Props {
  topology: Topology | null;
  frameRef: React.MutableRefObject<Frame | null>;
  selected: number | null;
  onSelect: (index: number | null) => void;
  /** Bodies whose paths are traced. */
  traced: number[];
  resetToken: number;
}

/** Picks a "nice" grid spacing (1, 2 or 5 x 10^n) giving roughly `target` lines across `span`. */
function niceStep(span: number, target: number): number {
  const raw = span / target;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

export function Viewport({ topology, frameRef, selected, onSelect, traced, resetToken }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const trails = useRef(new Map<number, Float64Array>());
  const trailLen = useRef(new Map<number, number>());
  const lastSteps = useRef(-1);
  const transform = useRef({ s: 1, ox: 0, oy: 0 });

  useEffect(() => {
    trails.current.clear();
    trailLen.current.clear();
    lastSteps.current = -1;
  }, [topology, resetToken, traced.join(',')]);

  useEffect(() => {
    let raf = 0;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const canvas = canvasRef.current;
      const frame = frameRef.current;
      if (!canvas) return;
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(h * dpr);
      }
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = C.sheet;
      ctx.fillRect(0, 0, w, h);
      if (!topology) return;

      // World -> screen: fit the scenario's view box, y up, uniform scale.
      const m = topology.meta;
      const pad = 40;
      const s = Math.min((w - 2 * pad) / (m.xmax - m.xmin), (h - 2 * pad) / (m.ymax - m.ymin));
      const ox = w / 2 - s * (m.xmin + m.xmax) / 2;
      const oy = h / 2 + s * (m.ymin + m.ymax) / 2;
      transform.current = { s, ox, oy };
      const X = (x: number) => ox + s * x;
      const Y = (y: number) => oy - s * y;

      // Graph paper with a true scale: minor and major lines in world units.
      const wx0 = (0 - ox) / s, wx1 = (w - ox) / s, wy0 = (oy - h) / s, wy1 = oy / s;
      const major = niceStep(Math.max(wx1 - wx0, wy1 - wy0), 6);
      const minor = major / 5;
      ctx.lineWidth = 1;
      for (const [step, color] of [[minor, C.gridMinor], [major, C.gridMajor]] as const) {
        ctx.strokeStyle = color;
        ctx.beginPath();
        for (let gx = Math.ceil(wx0 / step) * step; gx <= wx1; gx += step) {
          const px = Math.round(X(gx)) + 0.5;
          ctx.moveTo(px, 0); ctx.lineTo(px, h);
        }
        for (let gy = Math.ceil(wy0 / step) * step; gy <= wy1; gy += step) {
          const py = Math.round(Y(gy)) + 0.5;
          ctx.moveTo(0, py); ctx.lineTo(w, py);
        }
        ctx.stroke();
      }
      // Scale bar: one major division, labelled in the scenario's units.
      const unit = m.scenario === 'kepler' || m.scenario === 'figure-eight' || m.scenario === 'orbital-disk' ? '' : ' m';
      ctx.strokeStyle = C.ink;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(16, h - 16); ctx.lineTo(16 + major * s, h - 16);
      ctx.moveTo(16, h - 20); ctx.lineTo(16, h - 12);
      ctx.moveTo(16 + major * s, h - 20); ctx.lineTo(16 + major * s, h - 12);
      ctx.stroke();
      ctx.fillStyle = C.ink;
      ctx.font = '500 12px "Instrument Sans", system-ui, sans-serif';
      ctx.fillText(`${+major.toPrecision(3)}${unit}`, 22 + major * s, h - 12);

      if (m.walls) {
        ctx.strokeStyle = C.ink;
        ctx.lineWidth = 2.5;
        ctx.strokeRect(X(m.boxXmin), Y(m.boxYmax), s * (m.boxXmax - m.boxXmin), s * (m.boxYmax - m.boxYmin));
      }
      if (!frame) return;
      const { x, y } = frame;
      const n = Math.min(topology.count, x.length);

      // Trails: append on each new engine step count, draw as fading polylines.
      if (frame.diag.steps !== lastSteps.current) {
        lastSteps.current = frame.diag.steps;
        for (const i of traced) {
          if (i >= n) continue;
          let buf = trails.current.get(i);
          if (!buf) { buf = new Float64Array(TRAIL * 2); trails.current.set(i, buf); trailLen.current.set(i, 0); }
          const len = trailLen.current.get(i)!;
          if (len === TRAIL) buf.copyWithin(0, 2);
          const k = Math.min(len, TRAIL - 1);
          buf[2 * k] = x[i]; buf[2 * k + 1] = y[i];
          trailLen.current.set(i, Math.min(TRAIL, len + 1));
        }
      }
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      for (const [i, buf] of trails.current) {
        const len = trailLen.current.get(i) ?? 0;
        const segs = 6; // draw in a few batches with rising opacity
        for (let b = 0; b < segs; b++) {
          const a = Math.floor((b * len) / segs), z = Math.floor(((b + 1) * len) / segs);
          if (z - a < 2) continue;
          ctx.strokeStyle = `rgba(31, 79, 216, ${0.12 + 0.6 * ((b + 1) / segs)})`;
          ctx.beginPath();
          ctx.moveTo(X(buf[2 * a]), Y(buf[2 * a + 1]));
          for (let k = a + 1; k <= Math.min(z, len - 1); k++) ctx.lineTo(X(buf[2 * k]), Y(buf[2 * k + 1]));
          ctx.stroke();
        }
      }

      // Springs, coloured by strain (blue = compressed, red = stretched).
      const sp = topology.springs;
      if (sp.length) {
        ctx.lineWidth = 1.25;
        for (let k = 0; k < sp.length / 2; k++) {
          const a = sp[2 * k], b = sp[2 * k + 1];
          const len = Math.hypot(x[b] - x[a], y[b] - y[a]);
          const strain = Math.max(-1, Math.min(1, (len / topology.springRest[k] - 1) / 0.08));
          ctx.strokeStyle = strain >= 0
            ? `rgba(210, 58, 75, ${0.25 + 0.75 * strain})`
            : `rgba(31, 79, 216, ${0.25 - 0.75 * strain})`;
          ctx.beginPath();
          ctx.moveTo(X(x[a]), Y(y[a]));
          ctx.lineTo(X(x[b]), Y(y[b]));
          ctx.stroke();
        }
      }

      // Bodies. Heavier than the lightest -> filled; anchors -> solid square.
      let mMin = Infinity;
      for (let i = 0; i < n; i++) if (!topology.fixed[i]) mMin = Math.min(mMin, topology.mass[i]);
      for (let i = 0; i < n; i++) {
        const px = X(x[i]), py = Y(y[i]);
        const r = Math.max(2, topology.radius[i] * s);
        if (topology.fixed[i]) {
          ctx.fillStyle = C.ink;
          ctx.fillRect(px - r, py - r, 2 * r, 2 * r);
          continue;
        }
        ctx.beginPath();
        ctx.arc(px, py, r, 0, Math.PI * 2);
        const heavy = topology.mass[i] > mMin * 1.0001;
        ctx.fillStyle = heavy ? C.blue : r > 3 ? C.blueFill : C.ink;
        ctx.fill();
        if (r > 3) {
          ctx.lineWidth = 1;
          ctx.strokeStyle = heavy ? C.blue : C.ink;
          ctx.stroke();
        }
      }

      // Oscillator: the exact solution as a red ring, so the error is visible in place.
      if (frame.reference) {
        const r = Math.max(4, topology.radius[1] * s) + 4;
        ctx.strokeStyle = C.red;
        ctx.lineWidth = 2;
        ctx.setLineDash([4, 3]);
        ctx.beginPath();
        ctx.arc(X(frame.reference.x), Y(0), r, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);
      }

      if (selected !== null && selected < n) {
        const px = X(x[selected]), py = Y(y[selected]);
        const r = Math.max(6, topology.radius[selected] * s + 5);
        ctx.strokeStyle = C.red;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(px, py, r, 0, Math.PI * 2);
        ctx.moveTo(px - r - 6, py); ctx.lineTo(px - r + 2, py);
        ctx.moveTo(px + r - 2, py); ctx.lineTo(px + r + 6, py);
        ctx.moveTo(px, py - r - 6); ctx.lineTo(px, py - r + 2);
        ctx.moveTo(px, py + r - 2); ctx.lineTo(px, py + r + 6);
        ctx.stroke();
      }
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [topology, frameRef, selected, traced]);

  const pick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const frame = frameRef.current;
    if (!frame || !topology) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const { s, ox, oy } = transform.current;
    const cx = e.clientX - rect.left, cy = e.clientY - rect.top;
    let best = -1, bestD = 14 * 14;
    for (let i = 0; i < frame.x.length; i++) {
      const dx = ox + s * frame.x[i] - cx, dy = oy - s * frame.y[i] - cy;
      const r = Math.max(2, topology.radius[i] * s);
      const d = Math.max(0, Math.hypot(dx, dy) - r) ** 2;
      if (d < bestD) { bestD = d; best = i; }
    }
    onSelect(best >= 0 ? best : null);
  };

  return (
    <canvas
      ref={canvasRef}
      className="viewport-canvas"
      onClick={pick}
      role="img"
      aria-label="Simulation view. Click a body to inspect it."
    />
  );
}
