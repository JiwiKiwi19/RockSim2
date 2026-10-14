/// <reference lib="webworker" />
// Engine worker: owns the WebAssembly simulation and its fixed-step loop.
//
// Every ~16 ms it asks the engine to advance by (elapsed wall time x speed)
// simulated seconds. The engine takes whole steps of size dt (accumulator),
// so the step size never depends on the frame rate. A per-tick step budget,
// adapted from measured step cost, keeps each tick under ~10 ms; if the
// engine cannot keep up, simulated time runs slower than requested instead of
// queueing an ever-growing backlog (reported as droppedTime).
import createRockSim from './wasm/rocksim.mjs';
import type { RockSimModule, WasmSimulation } from './wasm/rocksim.mjs';
import type { Frame, FromWorker, LoadRequest, ToWorker } from './protocol';

declare const self: DedicatedWorkerGlobalScope;

let M: RockSimModule | null = null;
let sim: WasmSimulation | null = null;
let current: LoadRequest | null = null;
let running = false;
let speed = 1;
let lastTick = 0;
let msPerStep = 0.05; // running estimate, refined after each tick
let stepsThisSecond = 0;
let stepsPerSecond = 0;
let secondStart = 0;
const queue: ToWorker[] = [];

const post = (msg: FromWorker, transfer: Transferable[] = []) => self.postMessage(msg, transfer);

function copy<T extends Float64Array | Uint8Array | Uint32Array>(view: T): T {
  // Views alias WebAssembly memory, which can be replaced when it grows; copy now.
  return view.slice() as T;
}

function sendFrame() {
  if (!sim || !M || !current) return;
  const diag = sim.diagnostics();
  const frame: Frame = {
    x: copy(sim.x()), y: copy(sim.y()), vx: copy(sim.vx()), vy: copy(sim.vy()),
    diag, hash: sim.hash(), running: running && diag.status === 'ok', stepsPerSecond,
  };
  if (current.scenario === 'spring-oscillator') {
    const p = current.params;
    const mass = p.mass ?? 1, k = p.stiffness ?? 10, c = p.damping ?? 0, x0 = p.x0 ?? 0.5;
    frame.reference = { x: M.oscillatorExact(mass, k, c, x0, 0, diag.time).x, numericX: frame.x[1] };
  }
  post({ type: 'frame', frame }, [frame.x.buffer, frame.y.buffer, frame.vx.buffer, frame.vy.buffer]);
}

function load(req: LoadRequest) {
  if (!M) return;
  const next = new M.Simulation();
  const res = next.load(req.scenario, req.params, req.integrator, req.dt);
  if (!res.ok) {
    next.delete();
    post({ type: 'loaded', ok: false, errors: res.errors });
    return;
  }
  sim?.delete();
  sim = next;
  current = { ...req, params: { ...req.params } };
  running = false;
  msPerStep = 0.05;
  post({
    type: 'loaded', ok: true,
    topology: {
      meta: sim.meta(), count: sim.count(), radius: copy(sim.radius()), mass: copy(sim.mass()),
      fixed: copy(sim.fixed()), springs: copy(sim.springs()), springRest: copy(sim.springRest()),
    },
  });
  sendFrame();
}

function handle(msg: ToWorker) {
  if (!M) { queue.push(msg); return; }
  switch (msg.type) {
    case 'load': load(msg.req); break;
    case 'play': running = true; lastTick = performance.now(); break;
    case 'pause': running = false; sendFrame(); break;
    case 'step': running = false; sim?.stepN(Math.max(1, Math.min(10000, Math.floor(msg.n)))); sendFrame(); break;
    case 'speed': speed = Math.min(16, Math.max(1 / 64, msg.value)); break;
    case 'live': {
      if (!sim) break;
      const r = sim.setLiveParam(msg.key, msg.value);
      if (r.ok && current) current.params[msg.key] = msg.value;
      post({ type: 'live', key: msg.key, ok: r.ok, errors: r.errors });
      sendFrame();
      break;
    }
    case 'integrator': if (sim?.setIntegrator(msg.id) && current) current.integrator = msg.id; sendFrame(); break;
    case 'dt': if (sim?.setDt(msg.value) && current) current.dt = msg.value; break;
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
  const now = performance.now();
  if (now - secondStart >= 1000) {
    stepsPerSecond = (stepsThisSecond * 1000) / (now - secondStart);
    stepsThisSecond = 0;
    secondStart = now;
  }
  if (!running || !sim) return;
  const wall = Math.min(0.1, (now - lastTick) / 1000); // a background tab must not request minutes at once
  lastTick = now;
  const budgetMs = 10;
  const maxSteps = Math.max(1, Math.floor(budgetMs / Math.max(msPerStep, 1e-4)));
  const t0 = performance.now();
  const taken = sim.advance(wall * speed, maxSteps);
  const elapsed = performance.now() - t0;
  if (taken > 0) msPerStep = 0.8 * msPerStep + 0.2 * (elapsed / taken);
  stepsThisSecond += taken;
  sendFrame();
  if (sim.diagnostics().status !== 'ok') running = false;
}

self.onmessage = (e: MessageEvent<ToWorker>) => handle(e.data);

createRockSim()
  .then((mod) => {
    M = mod;
    post({ type: 'ready', catalog: M.catalog(), integrators: M.integrators() });
    secondStart = performance.now();
    setInterval(tick, 16);
    for (const m of queue.splice(0)) handle(m);
  })
  .catch((err: unknown) => post({ type: 'fatal', message: `The engine failed to start: ${String(err)}` }));
