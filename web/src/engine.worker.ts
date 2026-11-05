/// <reference lib="webworker" />
// Engine worker: owns the WebAssembly session and marshals messages.
// Every ~16 ms it passes the wall time to Session.tick(), which owns the time
// loop. Seeks, grab moves, body drags and live edits are coalesced so only the
// newest of each is applied per tick; everything else is applied on arrival.
import createRockSim from './wasm/rocksim.mjs';
import type { CommandResult, RockSimModule, WasmSession } from './wasm/rocksim.mjs';
import type { Frame, FromWorker, RunFrame, SeriesData, ToWorker } from './protocol';

declare const self: DedicatedWorkerGlobalScope;

let M: RockSimModule | null = null;
let session: WasmSession | null = null;
let lastTick = 0;
let lastComputeMs = 0;
let frameDirty = false;
let seriesDirty = false;
const queue: ToWorker[] = [];

type Pending = {
  seek?: { step?: number; time?: number };
  grabMove?: { run: number; x: number; y: number };
  body: Map<string, Extract<ToWorker, { type: 'setBody' }>>;
  live: Map<string, Extract<ToWorker, { type: 'live' }>>;
};
const pending: Pending = { body: new Map(), live: new Map() };

const post = (msg: FromWorker, transfer: Transferable[] = []) => self.postMessage(msg, transfer);

// Views alias WebAssembly memory, which can be replaced when it grows; copy now.
const copy = <T extends Float64Array | Uint8Array | Uint32Array>(view: T): T => view.slice() as T;

function sendFrame() {
  if (!session) return;
  const runs: RunFrame[] = [];
  const transfer: Transferable[] = [];
  for (let r = 0; r < session.runCount(); r++) {
    const run: RunFrame = {
      x: copy(session.x(r)), y: copy(session.y(r)), vx: copy(session.vx(r)), vy: copy(session.vy(r)),
      diag: session.diagnostics(r), hash: session.hash(r), liveParams: session.liveParams(r),
      trails: copy(session.trailPoints(r)), trailCounts: copy(session.trailCounts()), events: copy(session.events(r)),
    };
    transfer.push(run.x.buffer, run.y.buffer, run.vx.buffer, run.vy.buffer, run.trails.buffer, run.trailCounts.buffer, run.events.buffer);
    runs.push(run);
  }
  const frame: Frame = { runs, timeline: session.timeline(), traced: session.traced() };
  post({ type: 'frame', frame }, transfer);
  frameDirty = false;
}

function sendSeries() {
  if (!session) return;
  const runs: SeriesData[] = [];
  const transfer: Transferable[] = [];
  for (let r = 0; r < session.runCount(); r++) {
    const v = session.series(r);
    const s: SeriesData = {
      step: copy(v.step), t: copy(v.t), K: copy(v.K), U: copy(v.U), E: copy(v.E),
      rel: copy(v.rel), div: copy(v.div), numX: copy(v.numX), refX: copy(v.refX),
    };
    transfer.push(s.step.buffer, s.t.buffer, s.K.buffer, s.U.buffer, s.E.buffer, s.rel.buffer, s.div.buffer, s.numX.buffer, s.refX.buffer);
    runs.push(s);
  }
  post({ type: 'series', runs }, transfer);
  seriesDirty = false;
}

function touch() {
  frameDirty = true;
  seriesDirty = true;
}

function load(req: Extract<ToWorker, { type: 'load' }>['req']) {
  if (!M || !session) return;
  const res = session.load(req.scenario, req.params, req.integrator, req.dt);
  if (!res.ok) {
    post({ type: 'loaded', ok: false, errors: res.errors });
    return;
  }
  pending.seek = undefined;
  pending.grabMove = undefined;
  pending.body.clear();
  pending.live.clear();
  post({
    type: 'loaded', ok: true,
    topology: {
      meta: session.meta(), count: session.count(), radius: copy(session.radius()), mass: copy(session.mass()),
      fixed: copy(session.fixed()), springs: copy(session.springs()), springRest: copy(session.springRest()),
    },
  });
  touch();
  sendFrame();
  sendSeries();
}

function applySeek(s: { step?: number; time?: number }) {
  if (!session) return;
  if (s.time !== undefined) session.seekTime(s.time);
  else if (s.step !== undefined) session.seek(s.step);
}

/** Applies the coalesced input in the order the user produced it. */
function flush() {
  if (!session) return;
  if (pending.seek) { applySeek(pending.seek); pending.seek = undefined; touch(); }
  for (const m of pending.live.values()) report('live', session.setParam(m.run, m.key, m.value), m.key);
  pending.live.clear();
  for (const m of pending.body.values()) report('setBody', session.setBody(m.run, m.body, m.x, m.y, m.vx, m.vy));
  pending.body.clear();
  if (pending.grabMove) {
    const g = pending.grabMove;
    session.grabMove(g.run, g.x, g.y);
    pending.grabMove = undefined;
    frameDirty = true;
  }
}

function report(what: 'live' | 'integrator' | 'dt' | 'setBody' | 'grab', result: CommandResult, key?: string) {
  post({ type: 'result', what, key, result });
  touch();
}

function handle(msg: ToWorker) {
  if (!M || !session) { queue.push(msg); return; }
  // Order matters for a drag: queued moves land before the command that follows them.
  if (msg.type !== 'grabMove' && msg.type !== 'seek' && msg.type !== 'setBody' && msg.type !== 'live' && msg.type !== 'pick') flush();
  switch (msg.type) {
    case 'load': load(msg.req); break;
    case 'play': session.play(); lastTick = performance.now(); touch(); break;
    case 'pause': session.pause(); touch(); break;
    case 'step': session.step(Math.max(1, Math.min(10000, Math.floor(msg.n)))); touch(); break;
    case 'stepBack': session.stepBack(Math.max(1, Math.floor(msg.n))); touch(); break;
    case 'speed': session.setSpeed(msg.value); break;
    case 'seek': pending.seek = msg; break;
    case 'live': pending.live.set(`${msg.run}:${msg.key}`, msg); break;
    case 'setBody': pending.body.set(`${msg.run}:${msg.body}`, msg); break;
    case 'grabMove': pending.grabMove = msg; break;
    case 'integrator': report('integrator', session.setIntegrator(msg.run, msg.id)); break;
    case 'dt': report('dt', session.setDt(msg.run, msg.value)); break;
    case 'grabStart': report('grab', session.grabStart(msg.run, msg.body, msg.x, msg.y)); break;
    case 'grabEnd': session.grabEnd(msg.run); touch(); break;
    case 'selectBody': session.selectBody(msg.body); touch(); break;
    case 'pick': post({ type: 'picked', id: msg.id, body: session.pick(msg.run, msg.x, msg.y, msg.tolerance) }); break;
    case 'convergence': {
      const results = M.integrators().map((i) => M!.convergence(i.id, msg.damping, msg.levels));
      post({ type: 'convergence', results, damping: msg.damping });
      break;
    }
    case 'drift': {
      const results = M.integrators().map((i) => M!.energyDrift(i.id, msg.dt, msg.eccentricity, msg.orbits, 300));
      post({ type: 'drift', results, dt: msg.dt, eccentricity: msg.eccentricity, orbits: msg.orbits });
      break;
    }
  }
}

function tick() {
  if (!session) return;
  const now = performance.now();
  const wall = (now - lastTick) / 1000;
  lastTick = now;
  flush();
  const t0 = performance.now();
  const taken = session.tick(wall, lastComputeMs);
  lastComputeMs = performance.now() - t0;
  if (taken > 0) touch();
  if (frameDirty) sendFrame();
}

self.onmessage = (e: MessageEvent<ToWorker>) => handle(e.data);

createRockSim()
  .then((mod) => {
    M = mod;
    session = new M.Session();
    post({ type: 'ready', catalog: M.catalog(), integrators: M.integrators() });
    lastTick = performance.now();
    setInterval(tick, 16);
    setInterval(() => { if (seriesDirty) sendSeries(); }, 100);
    for (const m of queue.splice(0)) handle(m);
  })
  .catch((err: unknown) => post({ type: 'fatal', message: `The engine failed to start: ${String(err)}` }));
