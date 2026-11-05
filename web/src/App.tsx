import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type {
  ConvergenceResult, DriftResult, Frame, FromWorker, IntegratorSpec, ParamSpec, ScenarioSpec, SeriesData, Topology,
} from './protocol';
import {
  DivergencePanel, EnergyPanel, ErrorPanel, InspectorPanel, MeasurementsPanel, NotesPanel, OscillatorPanel,
} from './Measurements';
import { FieldState, ParametersPanel, validateDt, validateParam } from './SetupPanel';
import { SPEEDS, Timeline } from './Timeline';
import { ConvergencePanel, DriftPanel } from './VerifyView';
import { Viewport } from './Viewport';
import { useEngine } from './useEngine';
import { fmtTime } from './format';
import { Desktop } from './wm/Desktop';
import { MenuBar } from './wm/MenuBar';
import type { Menu } from './wm/MenuBar';
import { Window } from './wm/Window';
import { MOBILE_WIDTH, useWindows } from './wm/useWindows';
import type { Desk, WinDef } from './wm/useWindows';

const NON_DIMENSIONAL = new Set(['kepler', 'figure-eight', 'orbital-disk']);
const GAP = 12;

// Default arrangement, as fractions of the desktop: the scene and the tape on the
// left, controls and notes in the middle, instruments on the right.
function columns({ w, h }: Desk) {
  const aw = Math.round(w * 0.4), bw = Math.round(w * 0.23);
  const bx = GAP + aw + GAP, cx = bx + bw + GAP;
  const simH = Math.round(h * 0.66);
  const paramsH = Math.round(h * 0.5), inspectH = Math.round(h * 0.18);
  const measH = Math.round(h * 0.34), energyH = Math.round(h * 0.27);
  return { aw, bw, bx, cx, cw: w - cx - GAP, simH, paramsH, inspectH, measH, energyH, h };
}

const DEFS: WinDef[] = [
  { id: 'sim', title: 'Simulation', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: GAP, y: GAP, w: c.aw, h: c.simH }; } },
  { id: 'simB', title: 'Simulation B', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.cx, y: GAP, w: c.cw, h: Math.min(Math.round(d.h * 0.6), c.cw + 28) }; } },
  { id: 'timeline', title: 'Timeline', defaultOpen: true, rect: (d) => { const c = columns(d); const y = GAP + c.simH + GAP; return { x: GAP, y, w: c.aw, h: d.h - y - GAP }; } },
  { id: 'params', title: 'Parameters', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.bx, y: GAP, w: c.bw, h: c.paramsH }; } },
  { id: 'measure', title: 'Measurements', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.cx, y: GAP, w: c.cw, h: c.measH }; } },
  { id: 'energy', title: 'Energy', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.cx, y: GAP + c.measH + GAP, w: c.cw, h: c.energyH }; } },
  { id: 'oscillator', title: 'Oscillator Position', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.cx + 24, y: GAP + c.measH + GAP + 40, w: c.cw - 24, h: c.energyH }; } },
  { id: 'error', title: 'Relative Error', defaultOpen: true, rect: (d) => { const c = columns(d); const y = GAP + c.measH + GAP + c.energyH + GAP; return { x: c.cx, y, w: c.cw, h: d.h - y - GAP }; } },
  { id: 'divergence', title: 'Divergence', defaultOpen: true, rect: (d) => { const c = columns(d); const y = GAP + c.measH + GAP + c.energyH + GAP; return { x: c.cx + 24, y: y + 24, w: c.cw - 24, h: d.h - y - GAP - 24 }; } },
  { id: 'inspector', title: 'Inspector', defaultOpen: true, rect: (d) => { const c = columns(d); return { x: c.bx, y: GAP + c.paramsH + GAP, w: c.bw, h: c.inspectH }; } },
  { id: 'notes', title: 'Notes', defaultOpen: true, rect: (d) => { const c = columns(d); const y = GAP + c.paramsH + GAP + c.inspectH + GAP; return { x: c.bx, y, w: c.bw, h: d.h - y - GAP }; } },
  { id: 'convergence', title: 'Convergence Study', defaultOpen: false, rect: (d) => ({ x: GAP, y: GAP, w: Math.round(d.w * 0.5) - GAP, h: d.h - 2 * GAP }) },
  { id: 'drift', title: 'Energy Drift Study', defaultOpen: false, rect: (d) => ({ x: Math.round(d.w * 0.5), y: GAP, w: Math.round(d.w * 0.5) - GAP, h: d.h - 2 * GAP }) },
  { id: 'help', title: 'Help', defaultOpen: false, rect: (d) => ({ x: Math.round(d.w * 0.3), y: Math.round(d.h * 0.12), w: 500, h: Math.round(d.h * 0.7) }) },
];

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

  const [scenarioId, setScenarioId] = useState('particle-box');
  const [fieldsByScenario, setFieldsByScenario] = useState<Record<string, Record<string, FieldState>>>({});
  const [integrator, setIntegrator] = useState('verlet');
  const [dt, setDt] = useState<FieldState>({ text: '0.001', error: null });
  const [appliedParams, setAppliedParams] = useState<Record<string, number>>({});
  const [loadErrors, setLoadErrors] = useState<string[]>([]);
  const [liveErrors, setLiveErrors] = useState<Record<string, string>>({});

  const [topology, setTopology] = useState<Topology | null>(null);
  const frameRef = useRef<Frame | null>(null);
  const seriesRef = useRef<SeriesData[] | null>(null);
  const [frame, setFrame] = useState<Frame | null>(null); // throttled copy for React-rendered readouts
  const [series, setSeries] = useState<SeriesData[] | null>(null);
  const [version, setVersion] = useState(0);
  const [selected, setSelected] = useState<number | null>(null);
  const [speed, setSpeed] = useState(1);
  const [resetToken, setResetToken] = useState(0);
  const [activeRun, setActiveRun] = useState(0);
  const [notice, setNotice] = useState<string | null>(null);

  const [convergence, setConvergence] = useState<{ results: ConvergenceResult[]; damping: number } | null>(null);
  const [drift, setDrift] = useState<{ results: DriftResult[]; dt: number; eccentricity: number; orbits: number } | null>(null);
  const [busy, setBusy] = useState<'convergence' | 'drift' | null>(null);

  const [desk, setDesk] = useState<Desk>({ w: 0, h: 0 });
  const pickResolvers = useRef(new Map<number, (body: number) => void>());
  const pickId = useRef(1);
  const hadB = useRef(false);

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
          setActiveRun(0);
          setNotice(null);
          hadB.current = false;
          setResetToken((t) => t + 1);
        } else {
          setLoadErrors(msg.errors);
        }
        break;
      case 'frame':
        frameRef.current = msg.frame;
        break;
      case 'series':
        seriesRef.current = msg.runs;
        break;
      case 'result':
        if (msg.what === 'live' && msg.key) {
          const key = msg.key;
          setLiveErrors((e) => {
            const next = { ...e };
            if (msg.result.ok) delete next[key];
            else next[key] = msg.result.errors.join(' ');
            return next;
          });
        }
        if (msg.result.branched) {
          setNotice(hadB.current
            ? 'A new branch replaced run B. A keeps its future.'
            : 'Run B branched off here. A keeps its future; both runs now advance together.');
          hadB.current = true;
          setActiveRun(1);
        }
        break;
      case 'picked':
        pickResolvers.current.get(msg.id)?.(msg.body);
        pickResolvers.current.delete(msg.id);
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

  // React-side refresh at 10 Hz; the canvases redraw every animation frame on their own.
  useEffect(() => {
    const id = setInterval(() => {
      setFrame(frameRef.current);
      setSeries(seriesRef.current);
      setVersion((v) => v + 1);
    }, 100);
    return () => clearInterval(id);
  }, []);

  const pick = useCallback((run: number, x: number, y: number, tolerance: number) => new Promise<number>((resolve) => {
    const id = pickId.current++;
    pickResolvers.current.set(id, resolve);
    send({ type: 'pick', id, run, x, y, tolerance });
  }), [send]);

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
    const f = fieldsByScenario[s.id] ?? defaultsFor(s);
    setIntegrator(s.defaultIntegrator);
    setDt({ text: String(s.defaultDt), error: null });
    load(s, f, s.defaultIntegrator, s.defaultDt);
  };

  const run = frame && activeRun < frame.runs.length ? activeRun : 0;
  const branched = (frame?.runs.length ?? 1) > 1;

  // The timeline can change the integrator, dt and live parameters under the controls: follow the engine.
  const engineIntegrator = frame?.runs[run]?.diag.integrator;
  const engineDt = frame?.runs[run]?.diag.dt;
  useEffect(() => { if (engineIntegrator) setIntegrator(engineIntegrator); }, [engineIntegrator]);
  useEffect(() => {
    if (engineDt !== undefined) setDt((cur) => (Number(cur.text) === engineDt ? cur : { text: String(engineDt), error: null }));
  }, [engineDt]);

  const onField = (p: ParamSpec, text: string) => {
    const error = validateParam(p, text);
    setFieldsByScenario((all) => ({ ...all, [scenarioId]: { ...fields, [p.key]: { text, error } } }));
    if (!error && p.live) send({ type: 'live', run, key: p.key, value: Number(text) });
  };

  const onDt = (text: string) => {
    const error = validateDt(text);
    setDt({ text, error });
    if (!error) send({ type: 'dt', run, value: Number(text) });
  };

  const onIntegrator = (id: string) => {
    setIntegrator(id);
    send({ type: 'integrator', run, id });
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

  const select = (i: number | null) => {
    setSelected(i);
    send({ type: 'selectBody', body: i ?? -1 });
  };

  const onSpeed = (v: number) => {
    setSpeed(v);
    send({ type: 'speed', value: v });
  };

  const seekTime = useCallback((time: number) => send({ type: 'seek', time }), [send]);
  const running = frame?.timeline.running ?? false;
  const dimensionless = NON_DIMENSIONAL.has(scenarioId);
  const gravity = scenario?.params.find((p) => p.key === 'gravity' && p.live);

  const defs = DEFS;
  const { layout, mobile, place, raise, setOpen, patch, reset } = useWindows(defs, desk);

  const available = (id: string) => {
    if (id === 'simB' || id === 'divergence') return branched;
    if (id === 'oscillator') return scenarioId === 'spring-oscillator';
    return true;
  };
  const openWin = (id: string) => setOpen(id, true);
  const openVerify = (id: 'convergence' | 'drift') => {
    send({ type: 'pause' });
    openWin(id);
  };

  // The new branch should be visible.
  useEffect(() => { if (branched) setOpen('simB', true); }, [branched, setOpen]);

  const menus: Menu[] = [
    {
      label: 'Scene',
      items: [
        ...catalog.map((s) => ({ label: s.title, kind: 'radio' as const, checked: s.id === scenarioId, onSelect: () => chooseScenario(s) })),
        { separator: true as const },
        { label: 'Restart scene', onSelect: restart, shortcut: 'R' },
      ],
    },
    {
      label: 'Run',
      items: [
        { label: running ? 'Pause' : 'Run', onSelect: () => send({ type: running ? 'pause' : 'play' }), shortcut: 'Space' },
        { label: 'Step', onSelect: () => send({ type: 'step', n: 1 }), shortcut: 'S' },
        { label: 'Step 100', onSelect: () => send({ type: 'step', n: 100 }) },
        { label: 'Step back', onSelect: () => send({ type: 'stepBack', n: 1 }), shortcut: '[' },
        { label: 'Step forward', onSelect: () => send({ type: 'step', n: 1 }), shortcut: ']' },
        { label: 'Rewind 1 s', onSelect: () => seekTime(Math.max(0, (frame?.timeline.cursorTime ?? 0) - 1)) },
        { separator: true as const },
        ...SPEEDS.map((v) => ({ label: `Speed ${v}×`, kind: 'radio' as const, checked: speed === v, onSelect: () => onSpeed(v) })),
      ],
    },
    {
      label: 'Window',
      items: [
        ...defs.filter((d) => available(d.id) && d.id !== 'convergence' && d.id !== 'drift' && d.id !== 'help').map((d) => ({
          label: d.title, kind: 'check' as const, checked: !!layout?.[d.id]?.open, onSelect: () => setOpen(d.id, !layout?.[d.id]?.open),
        })),
        { separator: true as const },
        { label: 'Reset layout', onSelect: reset },
      ],
    },
    {
      label: 'Verify',
      items: [
        { label: 'Convergence study', onSelect: () => openVerify('convergence') },
        { label: 'Energy drift study', onSelect: () => openVerify('drift') },
      ],
    },
    { label: 'Help', items: [{ label: 'Keyboard and mouse', onSelect: () => openWin('help') }, { label: 'About this scene', onSelect: () => openWin('notes') }] },
  ];

  // Keyboard: space runs or pauses, S steps, [ and ] step back and forward, R restarts (ignored while typing).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      if (['INPUT', 'TEXTAREA', 'BUTTON', 'SELECT'].includes(t.tagName) || t.closest('[role=menu]')) return;
      if (e.key === ' ') {
        e.preventDefault();
        send({ type: running ? 'pause' : 'play' });
      } else if (e.key === 's' || e.key === ']') send({ type: 'step', n: 1 });
      else if (e.key === '[') send({ type: 'stepBack', n: 1 });
      else if (e.key === 'r') restart();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  // Notices fade out by themselves.
  useEffect(() => {
    if (!notice) return;
    const id = setTimeout(() => setNotice(null), 9000);
    return () => clearTimeout(id);
  }, [notice]);

  const error = fatal ?? failed;
  if (error) {
    return (
      <div className="fatal" role="alert">
        <h1>ROCsim2</h1>
        <p>{error}</p>
        <p>This page needs a browser with WebAssembly and module workers: current Chrome, Edge, Firefox or Safari.</p>
      </div>
    );
  }

  const simBody = (r: number) => {
    const rf = frame?.runs[r];
    const status = rf?.diag.status ?? 'ok';
    return (
      <div className="viewport">
        <Viewport
          run={r} topology={topology} frameRef={frameRef} selected={selected} onSelect={select} pick={pick} send={send}
          gravity={gravity} resetToken={resetToken}
        />
        <div className="hud" aria-live="off">
          <span>
            {branched ? `${r === 0 ? 'A' : 'B'}  ` : ''}t = {rf ? fmtTime(rf.diag.time) : '0.000'}{dimensionless ? '' : ' s'}
          </span>
        </div>
        {status === 'unstable' && (
          <div className="unstable" role="alert">
            <strong>The simulation became unstable and was stopped.</strong>
            <p>{rf?.diag.statusMessage}</p>
            <div className="dialog-actions">
              <button className="btn btn-primary" onClick={restart}>Restart</button>
              <button className="btn" onClick={() => seekTime(Math.max(0, (frame?.timeline.cursorTime ?? 0) - 1))}>Rewind 1 s</button>
            </div>
          </div>
        )}
      </div>
    );
  };

  const chart = { series, frame, version, onSeek: seekTime, dimensionless };

  const content = (id: string) => {
    if (!scenario) return null;
    switch (id) {
      case 'sim': return simBody(0);
      case 'simB': return simBody(1);
      case 'timeline':
        return <Timeline frame={frame} send={send} speed={speed} onSpeed={onSpeed} onRestart={restart} ready={!!topology} dimensionless={dimensionless} />;
      case 'params':
        return (
          <ParametersPanel
            integrators={integrators} params={scenario.params} integrator={integrator} onIntegrator={onIntegrator}
            dt={dt} onDt={onDt} fields={fields} onField={onField} live={frame?.runs[run]?.liveParams}
            pending={pending} onApply={restart} loadErrors={loadErrors} liveErrors={liveErrors}
            dimensionless={dimensionless} runLabel={branched ? (run === 0 ? 'A' : 'B') : null}
          />
        );
      case 'measure': return <MeasurementsPanel frame={frame} topology={topology} dimensionless={dimensionless} />;
      case 'energy': return <EnergyPanel {...chart} />;
      case 'oscillator': return <OscillatorPanel {...chart} />;
      case 'error': return <ErrorPanel {...chart} />;
      case 'divergence': return <DivergencePanel {...chart} />;
      case 'inspector':
        return <InspectorPanel frame={frame} topology={topology} selected={selected} run={run} onClear={() => select(null)} dimensionless={dimensionless} />;
      case 'notes': return <NotesPanel scenario={scenario} />;
      case 'convergence':
        return (
          <ConvergencePanel
            integrators={integrators} convergence={convergence} busy={busy === 'convergence'}
            onRun={(damping) => { setBusy('convergence'); send({ type: 'convergence', damping, levels: 7 }); }}
          />
        );
      case 'drift':
        return (
          <DriftPanel
            integrators={integrators} drift={drift} busy={busy === 'drift'}
            onRun={(dtv, orbits) => { setBusy('drift'); send({ type: 'drift', dt: dtv, eccentricity: 0.6, orbits }); }}
          />
        );
      case 'help': return <HelpPanel />;
    }
    return null;
  };

  return (
    <div className="app">
      <Desktop onSize={setDesk} mobile={mobile && desk.w < MOBILE_WIDTH}>
        <div className="wordmark" aria-hidden="true">
          <svg width="40" height="40" viewBox="0 0 32 32">
            <path className="logo-curve" d="M5 23 C 10 6, 22 6, 27 23" fill="none" strokeWidth="2.5" />
            <circle className="logo-dot" cx="27" cy="23" r="3.6" />
          </svg>
          <span>ROCsim2</span>
        </div>
        {catalog.length === 0 || !layout ? (
          <p className="loading" aria-busy="true">Starting the WebAssembly engine…</p>
        ) : (
          defs.map((d) => {
            const st = layout[d.id];
            if (!st || !st.open || !available(d.id)) return null;
            return (
              <Window
                key={d.id}
                title={d.title}
                state={st}
                mobile={mobile}
                className={`win-${d.id}`}
                onPlace={(r) => place(d.id, r)}
                onRaise={() => {
                  raise(d.id);
                  if (d.id === 'sim') setActiveRun(0);
                  else if (d.id === 'simB') setActiveRun(1);
                }}
                onCollapse={() => patch(d.id, { collapsed: !st.collapsed })}
                onClose={() => setOpen(d.id, false)}
              >
                {content(d.id)}
              </Window>
            );
          })
        )}
      </Desktop>
      <div className="statusbar">
        <MenuBar menus={menus} />
        <p className="status-note" role="status">{notice}</p>
      </div>
    </div>
  );
}

function HelpPanel() {
  return (
    <div className="notes help">
      <h3>Mouse and touch</h3>
      <ul>
        <li>Click a body to inspect it.</li>
        <li>Drag a body while the simulation runs: you hold it with a spring and damper (the red arrow is your force). Let go to fling it.</li>
        <li>Drag a body while paused: it moves where you put it.</li>
        <li>With a body selected and the run paused, drag the tip of its blue velocity arrow to change its velocity.</li>
        <li>Drag empty space to pan. Scroll or pinch to zoom at the pointer.</li>
        <li>Where gravity can change, drag the arrow tip in the corner of the view.</li>
        <li>Drag across any number in Parameters to scrub it.</li>
      </ul>
      <h3>Keyboard</h3>
      <ul>
        <li>Space runs or pauses. S or ] steps forward, [ steps back. R restarts.</li>
        <li>On the view: + and − zoom, arrow keys pan, 0 resets the view.</li>
        <li>On the tape: arrow keys step, Home and End jump to the ends. On a title bar: arrow keys move the window.</li>
      </ul>
      <h3>Time travel</h3>
      <p>
        Drag the tape or click a chart to rewind. If you change something in the past, a second run, B, branches off
        and A keeps its future. A third branch replaces B. The shaded part of the tape is too old to reach.
      </p>
      <p>The blue arrow's length is the distance the body would travel in a fixed short time, so longer means faster.</p>
    </div>
  );
}
