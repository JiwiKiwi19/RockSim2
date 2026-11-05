import { useEffect, useRef } from 'react';
import type { Frame, ParamSpec, ToWorker, Topology } from './protocol';
import { alpha, token } from './theme';

// Arrow length in world units is speed x this many seconds, per scenario.
const VELOCITY_SECONDS: Record<string, number> = {
  'particle-box': 0.25, 'spring-oscillator': 0.3, 'soft-body': 0.1, kepler: 0.15, 'figure-eight': 0.2, 'orbital-disk': 0.2,
};
const GRAVITY_PX_PER_UNIT = 6;
const GRAVITY_ANCHOR = { x: 34, y: 46 };
const HIT_PX = 14;
const CLICK_PX = 4;
const MIN_ZOOM = 0.2, MAX_ZOOM = 80;

interface Props {
  run: number;
  topology: Topology | null;
  frameRef: React.MutableRefObject<Frame | null>;
  selected: number | null;
  onSelect: (index: number | null) => void;
  /** Asks the worker which body is at a world point (within a tolerance in world units). */
  pick: (run: number, x: number, y: number, tolerance: number) => Promise<number>;
  send: (msg: ToWorker) => void;
  /** The gravity parameter, when this scenario lets it change live. */
  gravity: ParamSpec | undefined;
  resetToken: number;
}

/** Picks a "nice" grid spacing (1, 2 or 5 x 10^n) giving roughly `target` lines across `span`. */
function niceStep(span: number, target: number): number {
  const raw = span / target;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

type Mode = 'pending' | 'armed' | 'grab' | 'move' | 'pan' | 'velocity' | 'gravity' | 'pinch';
interface Gesture {
  id: number;
  mode: Mode;
  startX: number; startY: number;      // screen px at pointer-down
  moved: boolean;
  released: boolean;
  body: number;
  offX: number; offY: number;          // body position minus pointer, in world units
  pan0: { x: number; y: number };
}

export function Viewport(props: Props) {
  const P = useRef(props);
  P.current = props;
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const view = useRef({ zoom: 1, px: 0, py: 0 });
  const tf = useRef({ s: 1, ox: 0, oy: 0 });
  const gesture = useRef<Gesture | null>(null);
  const pointers = useRef(new Map<number, { x: number; y: number }>());
  const pinch = useRef<{ d: number; zoom: number; wx: number; wy: number } | null>(null);
  const grabTarget = useRef<{ x: number; y: number } | null>(null);
  const nextPick = useRef(1);

  useEffect(() => { view.current = { zoom: 1, px: 0, py: 0 }; }, [props.resetToken]);

  // ---------------------------------------------------------------- drawing
  useEffect(() => {
    let raf = 0;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const { topology, frameRef, run, selected, gravity } = P.current;
      const canvas = canvasRef.current;
      if (!canvas) return;
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (w === 0 || h === 0) return;
      if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(h * dpr);
      }
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      const ink = token('--ink'), paper = token('--paper'), blue = token('--blue'), red = token('--red');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = paper;
      ctx.fillRect(0, 0, w, h);
      if (!topology) return;

      // World -> screen: fit the scenario's view box, y up, uniform scale, then the user's zoom and pan.
      const m = topology.meta;
      const pad = 40;
      const s0 = Math.min((w - 2 * pad) / (m.xmax - m.xmin), (h - 2 * pad) / (m.ymax - m.ymin));
      const s = s0 * view.current.zoom;
      const cx = (m.xmin + m.xmax) / 2 + view.current.px, cy = (m.ymin + m.ymax) / 2 + view.current.py;
      const ox = w / 2 - s * cx, oy = h / 2 + s * cy;
      tf.current = { s, ox, oy };
      const X = (x: number) => ox + s * x;
      const Y = (y: number) => oy - s * y;

      // Dot grid at the major intersections: true scale in world units.
      const wx0 = (0 - ox) / s, wx1 = (w - ox) / s, wy0 = (oy - h) / s, wy1 = oy / s;
      const major = niceStep(Math.max(wx1 - wx0, wy1 - wy0), 6);
      ctx.fillStyle = token('--grid');
      for (let gx = Math.ceil(wx0 / major) * major; gx <= wx1; gx += major)
        for (let gy = Math.ceil(wy0 / major) * major; gy <= wy1; gy += major)
          ctx.fillRect(Math.round(X(gx)) - 1, Math.round(Y(gy)) - 1, 3, 3);

      if (m.walls) {
        ctx.strokeStyle = ink;
        ctx.lineWidth = 3;
        ctx.strokeRect(X(m.boxXmin), Y(m.boxYmax), s * (m.boxXmax - m.boxXmin), s * (m.boxYmax - m.boxYmin));
      }

      // Scale bar: one major division, labelled in the scenario's units.
      const unit = m.scenario === 'kepler' || m.scenario === 'figure-eight' || m.scenario === 'orbital-disk' ? '' : ' m';
      const bx = 16, by = h - 18, bl = major * s;
      ctx.fillStyle = ink;
      ctx.fillRect(bx, by - 1, bl, 3);
      ctx.fillRect(bx, by - 6, 2, 13);
      ctx.fillRect(bx + bl - 2, by - 6, 2, 13);
      ctx.font = '16px "VT323", monospace';
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(`${+major.toPrecision(3)}${unit}`, bx + bl + 8, by + 4);

      const frame = frameRef.current;
      const rf = frame?.runs[run];
      if (!frame || !rf) return;
      const { x, y } = rf;
      const n = Math.min(topology.count, x.length);

      // Trails: dotted polylines recorded by the engine at a fixed step stride.
      ctx.strokeStyle = blue;
      ctx.lineWidth = 2;
      ctx.setLineDash([2, 5]);
      let off = 0;
      for (const count of rf.trailCounts) {
        if (count > 1) {
          ctx.beginPath();
          ctx.moveTo(X(rf.trails[2 * off]), Y(rf.trails[2 * off + 1]));
          for (let k = 1; k < count; k++) ctx.lineTo(X(rf.trails[2 * (off + k)]), Y(rf.trails[2 * (off + k) + 1]));
          ctx.stroke();
        }
        off += count;
      }
      ctx.setLineDash([]);

      // Springs, coloured by strain (blue = compressed, red = stretched).
      const sp = topology.springs;
      if (sp.length) {
        ctx.lineWidth = 1.5;
        for (let k = 0; k < sp.length / 2; k++) {
          const a = sp[2 * k], b = sp[2 * k + 1];
          const dx = x[b] - x[a], dy = y[b] - y[a];
          const len = Math.sqrt(dx * dx + dy * dy);
          const strain = Math.max(-1, Math.min(1, (len / topology.springRest[k] - 1) / 0.08));
          ctx.strokeStyle = strain >= 0 ? alpha(red, 0.3 + 0.7 * strain) : alpha(blue, 0.3 - 0.7 * strain);
          ctx.beginPath();
          ctx.moveTo(X(x[a]), Y(y[a]));
          ctx.lineTo(X(x[b]), Y(y[b]));
          ctx.stroke();
        }
      }

      // Bodies at true scale. Anchors are squares; bodies heavier than the lightest are filled blue.
      let mMin = Infinity;
      for (let i = 0; i < n; i++) if (!topology.fixed[i]) mMin = Math.min(mMin, topology.mass[i]);
      for (let i = 0; i < n; i++) {
        const px = X(x[i]), py = Y(y[i]);
        const r = Math.max(2, topology.radius[i] * s);
        if (topology.fixed[i]) {
          ctx.fillStyle = ink;
          ctx.fillRect(px - r, py - r, 2 * r, 2 * r);
          continue;
        }
        const heavy = topology.mass[i] > mMin * 1.0001;
        ctx.beginPath();
        ctx.arc(px, py, r, 0, Math.PI * 2);
        ctx.fillStyle = heavy ? blue : r > 3 ? paper : ink;
        ctx.fill();
        if (r > 3) {
          ctx.lineWidth = 2;
          ctx.strokeStyle = ink;
          ctx.stroke();
        }
      }

      // Oscillator: the exact solution as a red dashed ring, so the error is visible in place.
      if (rf.diag.referenceX !== undefined) {
        const r = Math.max(4, topology.radius[1] * s) + 5;
        ctx.strokeStyle = red;
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 4]);
        ctx.beginPath();
        ctx.arc(X(rf.diag.referenceX), Y(0), r, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);
      }

      if (selected !== null && selected < n) {
        const px = X(x[selected]), py = Y(y[selected]);
        const r = Math.max(7, topology.radius[selected] * s + 6);
        ctx.strokeStyle = red;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(px - r - 7, py); ctx.lineTo(px - r + 1, py);
        ctx.moveTo(px + r - 1, py); ctx.lineTo(px + r + 7, py);
        ctx.moveTo(px, py - r - 7); ctx.lineTo(px, py - r + 1);
        ctx.moveTo(px, py + r - 1); ctx.lineTo(px, py + r + 7);
        ctx.stroke();

        // Velocity arrow; its tip is a handle while paused.
        if (!topology.fixed[selected]) {
          const tau = VELOCITY_SECONDS[m.scenario] ?? 0.2;
          const tx = X(x[selected] + rf.vx[selected] * tau), ty = Y(y[selected] + rf.vy[selected] * tau);
          arrow(ctx, px, py, tx, ty, blue, 2);
          if (!frame.timeline.running) {
            ctx.fillStyle = blue;
            ctx.fillRect(tx - 5, ty - 5, 10, 10);
          }
        }
      }

      // The force of the user's hand while a body is grabbed.
      const gt = grabTarget.current;
      if (gesture.current?.mode === 'grab' && gt && gesture.current.body < n) {
        const b = gesture.current.body;
        arrow(ctx, X(x[b]), Y(y[b]), X(gt.x), Y(gt.y), red, 3);
        ctx.strokeStyle = red;
        ctx.lineWidth = 2;
        ctx.strokeRect(X(gt.x) - 5, Y(gt.y) - 5, 10, 10);
      }

      // Gravity handle: drag the tip along the arrow to change g.
      if (gravity) {
        const g = rf.liveParams.gravity ?? gravity.default;
        const gx = GRAVITY_ANCHOR.x, gy = GRAVITY_ANCHOR.y, len = g * GRAVITY_PX_PER_UNIT;
        ctx.fillStyle = ink;
        ctx.fillRect(gx - 8, gy - 4, 16, 3);
        arrow(ctx, gx, gy, gx, gy + len, ink, 3);
        ctx.fillRect(gx - 7, gy + len - 1, 14, 10);
        ctx.fillStyle = paper;
        ctx.fillRect(gx - 5, gy + len + 1, 10, 6);
        ctx.fillStyle = ink;
        ctx.font = '16px "VT323", monospace';
        ctx.fillText(`g = ${g.toFixed(1)}`, gx + 14, gy + 6);
      }
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, []);

  // ---------------------------------------------------------------- input
  const toWorld = (px: number, py: number) => {
    const { s, ox, oy } = tf.current;
    return { x: (px - ox) / s, y: (oy - py) / s };
  };
  const local = (e: { clientX: number; clientY: number }) => {
    const r = canvasRef.current!.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  };

  const zoomAt = (sx: number, sy: number, factor: number) => {
    const v = view.current;
    const before = toWorld(sx, sy);
    v.zoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, v.zoom * factor));
    // Keep the world point under (sx, sy) fixed: recompute pan from the new scale.
    const m = P.current.topology?.meta;
    const canvas = canvasRef.current;
    if (!m || !canvas) return;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const s0 = Math.min((w - 80) / (m.xmax - m.xmin), (h - 80) / (m.ymax - m.ymin));
    const sn = s0 * v.zoom;
    v.px = before.x - (sx - w / 2) / sn - (m.xmin + m.xmax) / 2;
    v.py = before.y + (sy - h / 2) / sn - (m.ymin + m.ymax) / 2;
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const p = local(e);
      zoomAt(p.x, p.y, Math.exp(-e.deltaY * 0.0015));
    };
    canvas.addEventListener('wheel', onWheel, { passive: false });
    return () => canvas.removeEventListener('wheel', onWheel);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const endGrab = (g: Gesture) => {
    if (g.mode === 'grab') P.current.send({ type: 'grabEnd', run: P.current.run });
    grabTarget.current = null;
  };

  const onPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    const canvas = e.currentTarget;
    canvas.focus();
    canvas.setPointerCapture(e.pointerId);
    const p = local(e);
    pointers.current.set(e.pointerId, p);

    if (pointers.current.size === 2) {
      if (gesture.current) endGrab(gesture.current);
      gesture.current = null;
      const [a, b] = [...pointers.current.values()];
      const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      const wp = toWorld(mid.x, mid.y);
      pinch.current = { d: Math.hypot(a.x - b.x, a.y - b.y), zoom: view.current.zoom, wx: wp.x, wy: wp.y };
      return;
    }

    const { topology, frameRef, run, selected, gravity } = P.current;
    const frame = frameRef.current;
    const rf = frame?.runs[run];
    const g: Gesture = {
      id: nextPick.current++, mode: 'pending', startX: p.x, startY: p.y, moved: false, released: false,
      body: -1, offX: 0, offY: 0, pan0: { x: view.current.px, y: view.current.py },
    };
    gesture.current = g;
    if (!topology || !rf) return;

    // Handles are tested locally; bodies are picked by the engine.
    if (gravity) {
      const gv = rf.liveParams.gravity ?? gravity.default;
      if (Math.abs(p.x - GRAVITY_ANCHOR.x) < HIT_PX && Math.abs(p.y - (GRAVITY_ANCHOR.y + gv * GRAVITY_PX_PER_UNIT)) < HIT_PX) {
        g.mode = 'gravity';
        return;
      }
    }
    if (selected !== null && !frame!.timeline.running && selected < rf.x.length && !topology.fixed[selected]) {
      const tau = VELOCITY_SECONDS[topology.meta.scenario] ?? 0.2;
      const { s, ox, oy } = tf.current;
      const tx = ox + s * (rf.x[selected] + rf.vx[selected] * tau), ty = oy - s * (rf.y[selected] + rf.vy[selected] * tau);
      if (Math.hypot(p.x - tx, p.y - ty) < HIT_PX) {
        g.mode = 'velocity';
        g.body = selected;
        return;
      }
    }
    const wp = toWorld(p.x, p.y);
    P.current.pick(run, wp.x, wp.y, HIT_PX / tf.current.s).then((body) => {
      const cur = gesture.current;
      if (!cur || cur.id !== g.id) {
        // The gesture ended before the engine answered: it was a click.
        if (g.released && !g.moved) P.current.onSelect(body >= 0 ? body : null);
        return;
      }
      cur.body = body;
      if (body < 0) { cur.mode = 'pan'; return; }
      const r2 = P.current.frameRef.current?.runs[run];
      if (r2) {
        const now = toWorld(cur.startX, cur.startY);
        cur.offX = r2.x[body] - now.x;
        cur.offY = r2.y[body] - now.y;
      }
      cur.mode = 'armed';
    });
  };

  const onPointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const p = local(e);
    if (pointers.current.has(e.pointerId)) pointers.current.set(e.pointerId, p);

    if (pinch.current && pointers.current.size >= 2) {
      const [a, b] = [...pointers.current.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      const v = view.current;
      const m = P.current.topology?.meta;
      const canvas = canvasRef.current;
      if (!m || !canvas) return;
      v.zoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, pinch.current.zoom * (d / pinch.current.d)));
      const w = canvas.clientWidth, h = canvas.clientHeight;
      const sn = Math.min((w - 80) / (m.xmax - m.xmin), (h - 80) / (m.ymax - m.ymin)) * v.zoom;
      v.px = pinch.current.wx - (mid.x - w / 2) / sn - (m.xmin + m.xmax) / 2;
      v.py = pinch.current.wy + (mid.y - h / 2) / sn - (m.ymin + m.ymax) / 2;
      return;
    }

    const g = gesture.current;
    if (!g) return;
    if (Math.hypot(p.x - g.startX, p.y - g.startY) > CLICK_PX) g.moved = true;
    const { run, send, topology, frameRef, gravity } = P.current;
    const wp = toWorld(p.x, p.y);
    const rf = frameRef.current?.runs[run];

    switch (g.mode) {
      case 'pan': {
        const s = tf.current.s;
        view.current.px = g.pan0.x - (p.x - g.startX) / s;
        view.current.py = g.pan0.y + (p.y - g.startY) / s;
        break;
      }
      case 'armed':
        if (!g.moved) break;
        if (frameRef.current?.timeline.running) {
          g.mode = 'grab';
          send({ type: 'grabStart', run, body: g.body, x: wp.x + g.offX, y: wp.y + g.offY });
        } else {
          g.mode = 'move';
        }
        break;
      case 'grab':
        grabTarget.current = { x: wp.x + g.offX, y: wp.y + g.offY };
        send({ type: 'grabMove', run, x: wp.x + g.offX, y: wp.y + g.offY });
        break;
      case 'velocity': {
        if (!rf || !topology || !g.moved) break;
        const tau = VELOCITY_SECONDS[topology.meta.scenario] ?? 0.2;
        send({
          type: 'setBody', run, body: g.body, x: rf.x[g.body], y: rf.y[g.body],
          vx: (wp.x - rf.x[g.body]) / tau, vy: (wp.y - rf.y[g.body]) / tau,
        });
        break;
      }
      case 'gravity': {
        if (!gravity) break;
        const v = (p.y - GRAVITY_ANCHOR.y) / GRAVITY_PX_PER_UNIT;
        const value = Math.min(gravity.max, Math.max(gravity.min, Math.round(v * 10) / 10));
        send({ type: 'live', run, key: 'gravity', value });
        break;
      }
    }
    if (g.mode === 'move' && rf) {
      send({
        type: 'setBody', run, body: g.body, x: wp.x + g.offX, y: wp.y + g.offY, vx: rf.vx[g.body], vy: rf.vy[g.body],
      });
    }
  };

  const onPointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    pointers.current.delete(e.pointerId);
    if (pinch.current) {
      if (pointers.current.size < 2) pinch.current = null;
      return;
    }
    const g = gesture.current;
    if (!g) return;
    g.released = true;
    gesture.current = null;
    endGrab(g);
    if (!g.moved && (g.mode === 'armed' || g.mode === 'pan' || g.mode === 'pending')) {
      // A click, not a drag. 'pending' is answered by the pick callback.
      if (g.mode !== 'pending') P.current.onSelect(g.mode === 'armed' ? g.body : null);
    }
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLCanvasElement>) => {
    const canvas = e.currentTarget;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const v = view.current;
    const step = 0.1 / v.zoom * (P.current.topology ? P.current.topology.meta.xmax - P.current.topology.meta.xmin : 1);
    switch (e.key) {
      case '+': case '=': zoomAt(w / 2, h / 2, 1.25); break;
      case '-': case '_': zoomAt(w / 2, h / 2, 0.8); break;
      case '0': view.current = { zoom: 1, px: 0, py: 0 }; break;
      case 'ArrowLeft': v.px -= step; break;
      case 'ArrowRight': v.px += step; break;
      case 'ArrowUp': v.py += step; break;
      case 'ArrowDown': v.py -= step; break;
      default: return;
    }
    e.preventDefault();
  };

  return (
    <canvas
      ref={canvasRef}
      className="viewport-canvas"
      tabIndex={0}
      role="application"
      aria-label={`Simulation ${props.run === 0 ? 'A' : 'B'} view. Click a body to inspect it. Drag a body to grab it, drag empty space to pan, scroll to zoom. Plus, minus, arrow keys and 0 adjust the view.`}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onKeyDown={onKeyDown}
    />
  );
}

function arrow(ctx: CanvasRenderingContext2D, x0: number, y0: number, x1: number, y1: number, color: string, width: number) {
  const dx = x1 - x0, dy = y1 - y0;
  const len = Math.sqrt(dx * dx + dy * dy);
  if (len < 3) return;
  const ux = dx / len, uy = dy / len;
  const head = Math.min(11, len * 0.6);
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo(x0, y0);
  ctx.lineTo(x1 - ux * head * 0.6, y1 - uy * head * 0.6);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x1 - ux * head - uy * head * 0.5, y1 - uy * head + ux * head * 0.5);
  ctx.lineTo(x1 - ux * head + uy * head * 0.5, y1 - uy * head - ux * head * 0.5);
  ctx.closePath();
  ctx.fill();
}
