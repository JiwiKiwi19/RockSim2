import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { History } from './history';
import { Measurements } from './Measurements';
import type {
  ConvergenceResult, DriftResult, Frame, FromWorker, IntegratorSpec, ParamSpec, ScenarioSpec, Topology,
} from './protocol';
import { FieldState, SetupPanel, validateDt, validateParam } from './SetupPanel';
import { useEngine } from './useEngine';
import { VerifyView } from './VerifyView';
import { Viewport } from './Viewport';
import { fmtTime } from './format';

const SPEEDS = [0.25, 0.5, 1, 2, 4];
const NON_DIMENSIONAL = new Set(['kepler', 'figure-eight', 'orbital-disk']);

type Mode = 'simulate' | 'verify';

function defaultsFor(s: ScenarioSpec): Record<string, FieldState> {
  return Object.fromEntries(s.params.map((p) => [p.key, { text: String(p.default), error: null }]));
}

function numericValues(fields: Record<string, FieldState>): Record<string, number> {
  return Object.fromEntries(Object.entries(fields).map(([k, f]) => [k, Number(f.text)]));
}

export default function App() {
  const [catalog, setCatalog] = useState<ScenarioSpec[]>([]);
  const [integrators, setIntegrators] = useState<IntegratorSpec[]>([]);
  const [fatal, setFatal] = useState<string | null>(null);
  const [mode, setMode] = useState<Mode>('simulate');

  const [scenarioId, setScenarioId] = useState('particle-box');
  const [fieldsByScenario, setFieldsByScenario] = useState<Record<string, Record<string, FieldState>>>({});
  const [integrator, setIntegrator] = useState('verlet');
  const [dt, setDt] = useState<FieldState>({ text: '0.001', error: null });
  const [appliedParams, setAppliedParams] = useState<Record<string, number>>({});
  const [loadErrors, setLoadErrors] = useState<string[]>([]);
  const [liveErrors, setLiveErrors] = useState<Record<string, string>>({});

  const [topology, setTopology] = useState<Topology | null>(null);
  const frameRef = useRef<Frame | null>(null);
  const [frame, setFrame] = useState<Frame | null>(null); // throttled copy for React-rendered readouts
  const history = useRef(new History());
  const [version, setVersion] = useState(0);
  const [selected, setSelected] = useState<number | null>(null);
  const [speed, setSpeed] = useState(1);
  const [resetToken, setResetToken] = useState(0);

  const [convergence, setConvergence] = useState<{ results: ConvergenceResult[]; damping: number } | null>(null);
  const [drift, setDrift] = useState<{ results: DriftResult[]; dt: number; eccentricity: number; orbits: number } | null>(null);
  const [busy, setBusy] = useState<'convergence' | 'drift' | null>(null);

  const scenario = catalog.find((s) => s.id === scenarioId) ?? null;
  const fields = useMemo(
    () => fieldsByScenario[scenarioId] ?? (scenario ? defaultsFor(scenario) : {}),
    [fieldsByScenario, scenarioId, scenario],
  );

  const onMessage = useCallback((msg: FromWorker) => {
    switch (msg.type) {
      case 'ready':
        setCatalog(msg.catalog);
        setIntegrators(msg.integrators);
        break;
      case 'fatal':
        setFatal(msg.message);
        break;
      case 'loaded':
        if (msg.ok) {
          setTopology(msg.topology);
          setLoadErrors([]);
          setLiveErrors({});
          setSelected(null);
          history.current.clear();
          setResetToken((t) => t + 1);
        } else {
          setLoadErrors(msg.errors);
        }
        break;
      case 'frame':
        frameRef.current = msg.frame;
        history.current.push(msg.frame);
        break;
      case 'live':
        setLiveErrors((e) => {
          const next = { ...e };
          if (msg.ok) delete next[msg.key];
          else next[msg.key] = msg.errors.join(' ');
          return next;
        });
        break;
      case 'convergence':
        setConvergence({ results: msg.results, damping: msg.damping });
        setBusy(null);
        break;
      case 'drift':
        setDrift({ results: msg.results, dt: msg.dt, eccentricity: msg.eccentricity, orbits: msg.orbits });
        setBusy(null);
        break;
    }
  }, []);

  const { send, failed } = useEngine(onMessage);

  // React-side refresh at 10 Hz; the canvas redraws every animation frame on its own.
  useEffect(() => {
    const id = setInterval(() => {
      setFrame(frameRef.current);
      setVersion((v) => v + 1);
    }, 100);
    return () => clearInterval(id);
  }, []);

  const load = useCallback((s: ScenarioSpec, f: Record<string, FieldState>, integ: string, dtValue: number) => {
    const params = numericValues(f);
    setAppliedParams(params);
    send({ type: 'load', req: { scenario: s.id, params, integrator: integ, dt: dtValue } });
  }, [send]);

  // First load once the catalogue arrives.
  useEffect(() => {
    const s = catalog.find((c) => c.id === scenarioId);
    if (s && !topology) {
      setIntegrator(s.defaultIntegrator);
      setDt({ text: String(s.defaultDt), error: null });
      load(s, defaultsFor(s), s.defaultIntegrator, s.defaultDt);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [catalog]);

  const chooseScenario = (s: ScenarioSpec) => {
    setScenarioId(s.id);
    setMode('simulate');
    const f = fieldsByScenario[s.id] ?? defaultsFor(s);
    setIntegrator(s.defaultIntegrator);
    setDt({ text: String(s.defaultDt), error: null });
    load(s, f, s.defaultIntegrator, s.defaultDt);
  };

  const onField = (p: ParamSpec, text: string) => {
    const error = validateParam(p, text);
    setFieldsByScenario((all) => ({ ...all, [scenarioId]: { ...fields, [p.key]: { text, error } } }));
    if (!error && p.live) send({ type: 'live', key: p.key, value: Number(text) });
  };

  const onDt = (text: string) => {
    const error = validateDt(text);
    setDt({ text, error });
    if (!error) send({ type: 'dt', value: Number(text) });
  };

  const onIntegrator = (id: string) => {
    setIntegrator(id);
    send({ type: 'integrator', id });
  };

  // Non-live parameters differ from what is running -> offer "Apply and restart".
  const pending = !!scenario && scenario.params.some((p) => {
    if (p.live) return false;
    const v = Number(fields[p.key]?.text);
    return Number.isFinite(v) && appliedParams[p.key] !== undefined && v !== appliedParams[p.key];
  });

  const restart = () => {
    if (!scenario || dt.error || Object.values(fields).some((f) => f.error)) return;
    load(scenario, fields, integrator, Number(dt.text));
  };

  const running = frame?.running ?? false;
  const status = frame?.diag.status ?? 'ok';
  const dimensionless = NON_DIMENSIONAL.has(scenarioId);

  const traced = useMemo(() => {
    if (!topology) return [];
    if (scenarioId === 'kepler' || scenarioId === 'figure-eight') return Array.from({ length: topology.count }, (_, i) => i);
    return selected !== null ? [selected] : [];
  }, [topology, scenarioId, selected]);

  // Keyboard: space toggles play, "s" steps, "r" restarts (ignored while typing).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (mode !== 'simulate' || t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'BUTTON') return;
      if (e.key === ' ') { e.preventDefault(); send({ type: running ? 'pause' : 'play' }); }
      else if (e.key === 's') send({ type: 'step', n: 1 });
      else if (e.key === 'r') restart();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  const error = fatal ?? failed;
  if (error) {
    return (
      <div className="fatal" role="alert">
        <h1>RockSim</h1>
        <p>{error}</p>
        <p>This page needs a browser with WebAssembly and module workers: current Chrome, Edge, Firefox or Safari.</p>
      </div>
    );
  }

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <svg width="28" height="28" viewBox="0 0 32 32" aria-hidden="true">
            <path d="M5 23 C 10 6, 22 6, 27 23" fill="none" stroke="#1F4FD8" strokeWidth="2.5" />
            <circle cx="27" cy="23" r="3.6" fill="#1B2430" />
          </svg>
          <span className="wordmark">RockSim</span>
        </div>
        <nav className="scenarios" aria-label="Scenarios">
          {catalog.map((s) => (
            <button
              key={s.id}
              className="tab"
              aria-current={mode === 'simulate' && s.id === scenarioId ? 'page' : undefined}
              onClick={() => chooseScenario(s)}
            >
              {s.title}
            </button>
          ))}
        </nav>
        <div className="segmented mode" role="group" aria-label="View">
          <button aria-pressed={mode === 'simulate'} onClick={() => setMode('simulate')}>Simulate</button>
          <button aria-pressed={mode === 'verify'} onClick={() => { setMode('verify'); send({ type: 'pause' }); }}>Verify</button>
        </div>
      </header>

      {catalog.length === 0 ? (
        <main className="loading" aria-busy="true"><p>Starting the WebAssembly engine…</p></main>
      ) : mode === 'verify' ? (
        <main className="verify-main">
          <VerifyView
            integrators={integrators}
            convergence={convergence}
            drift={drift}
            busy={busy}
            onRunConvergence={(damping) => { setBusy('convergence'); send({ type: 'convergence', damping, levels: 7 }); }}
            onRunDrift={(dtv, orbits) => { setBusy('drift'); send({ type: 'drift', dt: dtv, eccentricity: 0.6, orbits }); }}
          />
        </main>
      ) : (
        <main className="bench">
          {scenario && (
            <SetupPanel
              scenario={scenario}
              integrators={integrators}
              integrator={integrator}
              onIntegrator={onIntegrator}
              dt={dt}
              onDt={onDt}
              fields={fields}
              onField={onField}
              pending={pending}
              onApply={restart}
              loadErrors={loadErrors}
              liveErrors={liveErrors}
              dimensionless={dimensionless}
            />
          )}

          <section className="stage" aria-label="Simulation">
            <div className="viewport">
              <Viewport
                topology={topology}
                frameRef={frameRef}
                selected={selected}
                onSelect={setSelected}
                traced={traced}
                resetToken={resetToken}
              />
              <div className="hud" aria-live="off">
                <span>t = {frame ? fmtTime(frame.diag.time) : '0.000'}{dimensionless ? '' : ' s'}</span>
              </div>
              {status === 'unstable' && (
                <div className="unstable" role="alert">
                  <strong>The simulation became unstable and was stopped.</strong>
                  <p>{frame?.diag.statusMessage}</p>
                  <button className="btn btn-primary" onClick={restart}>Restart</button>
                </div>
              )}
            </div>
            <div className="transport">
              <button
                className="btn btn-primary play"
                disabled={status !== 'ok' || !topology}
                onClick={() => send({ type: running ? 'pause' : 'play' })}
              >
                {running ? 'Pause' : 'Run'}
              </button>
              <button className="btn btn-secondary" disabled={running || status !== 'ok'} onClick={() => send({ type: 'step', n: 1 })}>
                Step
              </button>
              <button className="btn btn-secondary" disabled={running || status !== 'ok'} onClick={() => send({ type: 'step', n: 100 })}>
                Step 100
              </button>
              <button className="btn btn-secondary" onClick={restart}>Restart</button>
              <div className="segmented speed" role="group" aria-label="Playback speed">
                {SPEEDS.map((v) => (
                  <button
                    key={v}
                    aria-pressed={speed === v}
                    onClick={() => { setSpeed(v); send({ type: 'speed', value: v }); }}
                  >
                    {v < 1 ? `${v === 0.25 ? '¼' : '½'}×` : `${v}×`}
                  </button>
                ))}
              </div>
              <p className="hint">Space runs or pauses, S steps, R restarts. Speed changes how much simulated time passes per second, never dt.</p>
            </div>
          </section>

          {scenario && (
            <Measurements
              scenario={scenario}
              frame={frame}
              topology={topology}
              history={history.current}
              version={version}
              selected={selected}
              onClearSelection={() => setSelected(null)}
              dimensionless={dimensionless}
            />
          )}
        </main>
      )}
    </div>
  );
}
