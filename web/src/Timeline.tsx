import { useRef, useState } from 'react';
import type { Frame, ToWorker } from './protocol';
import { fmtTime } from './format';

export const SPEEDS = [0.25, 0.5, 1, 2, 4];
const EVENT_NAMES = ['Grab', 'Grab drag', 'Release', 'Parameter', 'Time step', 'Integrator', 'Move body'];

interface Props {
  frame: Frame | null;
  send: (msg: ToWorker) => void;
  speed: number;
  onSpeed: (v: number) => void;
  onRestart: () => void;
  ready: boolean;
  dimensionless: boolean;
}

function niceStep(span: number, target: number): number {
  const raw = span / target;
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

/** Transport buttons and a tape strip in simulated time. */
export function Timeline({ frame, send, speed, onSpeed, onRestart, ready, dimensionless }: Props) {
  const strip = useRef<HTMLDivElement>(null);
  const [dragTime, setDragTime] = useState<number | null>(null);
  const tl = frame?.timeline;
  const runs = frame?.runs ?? [];
  const running = tl?.running ?? false;
  const unstable = runs.some((r) => r.diag.status !== 'ok');
  const replay = runs.some((r) => r.diag.steps < r.diag.headStep);
  const u = dimensionless ? '' : ' s';

  const headTime = Math.max(0, ...runs.map((r) => r.diag.headTime), tl?.cursorTime ?? 0);
  const span = Math.max(headTime, 1e-9);
  const shown = dragTime ?? tl?.cursorTime ?? 0;
  const pct = (t: number) => `${Math.min(100, Math.max(0, (t / span) * 100))}%`;
  const tick = niceStep(span, 5);
  const ticks: number[] = [];
  for (let t = 0; t <= span + 1e-12; t += tick) ticks.push(t);

  const timeAt = (clientX: number) => {
    const r = strip.current!.getBoundingClientRect();
    return Math.min(span, Math.max(0, ((clientX - r.left) / r.width) * span));
  };
  const seekTo = (clientX: number) => {
    const t = timeAt(clientX);
    setDragTime(t);
    send({ type: 'seek', time: t });
  };
  const onKey = (e: React.KeyboardEvent) => {
    if (!tl) return;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') send({ type: 'stepBack', n: e.shiftKey ? 100 : 1 });
    else if (e.key === 'ArrowRight' || e.key === 'ArrowUp') send({ type: 'step', n: e.shiftKey ? 100 : 1 });
    else if (e.key === 'Home') send({ type: 'seek', step: 0 });
    else if (e.key === 'End') send({ type: 'seek', step: tl.seekLimit });
    else return;
    e.preventDefault();
  };

  const idle = running || unstable || !ready;
  return (
    <div className="timeline">
      <div className="transport">
        <button className="btn btn-primary play" disabled={unstable || !ready} onClick={() => send({ type: running ? 'pause' : 'play' })}>
          {running ? 'Pause' : 'Run'}
        </button>
        <button className="btn" disabled={idle || (tl?.cursorStep ?? 0) <= (tl?.oldestStep ?? 0)} onClick={() => send({ type: 'stepBack', n: 1 })}>
          Step back
        </button>
        <button className="btn" disabled={idle} onClick={() => send({ type: 'step', n: 1 })}>Step</button>
        <button className="btn" disabled={idle} onClick={() => send({ type: 'step', n: 100 })}>Step 100</button>
        <button className="btn" disabled={!ready} onClick={onRestart}>Restart</button>
        <div className="segmented speed" role="group" aria-label="Playback speed">
          {SPEEDS.map((v) => (
            <button
              key={v}
              aria-pressed={speed === v}
              onClick={() => { onSpeed(v); }}
            >
              {v < 1 ? `${v === 0.25 ? '¼' : '½'}×` : `${v}×`}
            </button>
          ))}
        </div>
        <span className={`mode-badge ${replay ? 'is-replay' : 'is-live'}`} role="status">{replay ? 'REPLAY' : 'LIVE'}</span>
      </div>

      <div
        className="tape"
        ref={strip}
        role="slider"
        tabIndex={0}
        aria-label="Simulation time"
        aria-valuemin={tl?.oldestTime ?? 0}
        aria-valuemax={span}
        aria-valuenow={tl?.cursorTime ?? 0}
        aria-valuetext={`t = ${fmtTime(tl?.cursorTime ?? 0)}${u}, ${replay ? 'replay' : 'live'}`}
        onKeyDown={onKey}
        onPointerDown={(e) => {
          if (!tl) return;
          e.currentTarget.setPointerCapture(e.pointerId);
          e.currentTarget.focus();
          seekTo(e.clientX);
        }}
        onPointerMove={(e) => { if (e.currentTarget.hasPointerCapture(e.pointerId)) seekTo(e.clientX); }}
        onPointerUp={() => setDragTime(null)}
        onPointerCancel={() => setDragTime(null)}
      >
        <div className="tape-track tape-a" aria-hidden="true">
          <span className="tape-label">A</span>
          <div className="tape-bar" style={{ width: pct(runs[0]?.diag.headTime ?? 0) }} />
          {tl && tl.oldestTime > 0 && <div className="tape-lost" style={{ width: pct(tl.oldestTime) }} title="Older than the oldest stored keyframe: not reachable" />}
          {events(runs[0]?.events).map((ev, i) => (
            <i key={i} className={`mark kind-${ev.kind}`} style={{ left: pct(ev.time) }} title={`${EVENT_NAMES[ev.kind] ?? 'Action'} at t = ${fmtTime(ev.time)}${u}`} />
          ))}
        </div>
        {runs.length > 1 && (
          <div className="tape-track tape-b" aria-hidden="true">
            <span className="tape-label">B</span>
            <div className="tape-bar" style={{ left: pct(tl?.branchTime ?? 0), width: `calc(${pct(runs[1].diag.headTime)} - ${pct(tl?.branchTime ?? 0)})` }} />
            {events(runs[1].events).filter((ev) => ev.time >= (tl?.branchTime ?? 0)).map((ev, i) => (
              <i key={i} className={`mark kind-${ev.kind}`} style={{ left: pct(ev.time) }} title={`${EVENT_NAMES[ev.kind] ?? 'Action'} at t = ${fmtTime(ev.time)}${u}`} />
            ))}
          </div>
        )}
        <div className="tape-axis" aria-hidden="true">
          {ticks.map((t) => <span key={t} style={{ left: pct(t) }}>{+t.toPrecision(3)}</span>)}
        </div>
        <div className="tape-cursor" style={{ left: pct(shown) }} aria-hidden="true"><b>{fmtTime(shown)}</b></div>
      </div>
      <p className="hint">
        Drag the tape to rewind. Edit the past and a second run, B, branches off while A keeps its future. Space runs or pauses, S steps, [ and ] step back and forward, R restarts.
      </p>
    </div>
  );
}

function events(flat: Float64Array | undefined): { time: number; kind: number }[] {
  const out: { time: number; kind: number }[] = [];
  if (!flat) return out;
  // Consecutive events of one kind at the same time collapse into one mark.
  let last = -Infinity;
  for (let i = 0; i + 2 < flat.length; i += 3) {
    if (flat[i + 1] - last < 1e-9 && out.length && out[out.length - 1].kind === flat[i + 2]) continue;
    out.push({ time: flat[i + 1], kind: flat[i + 2] });
    last = flat[i + 1];
  }
  return out;
}
