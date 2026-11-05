import { useMemo, useState } from 'react';
import type uPlot from 'uplot';
import { Chart } from './Chart';
import type { ConvergenceResult, DriftResult, IntegratorSpec } from './protocol';
import { fmt } from './format';
import { token } from './theme';

const INTEGRATOR_TOKENS: Record<string, string> = {
  euler: '--red',
  'symplectic-euler': '--amber',
  verlet: '--blue',
  rk4: '--green',
};
const colorOf = (id: string) => token(INTEGRATOR_TOKENS[id] ?? '--ink');

const nameOf = (integrators: IntegratorSpec[], id: string) => integrators.find((i) => i.id === id)?.name ?? id;

interface ConvergenceProps {
  integrators: IntegratorSpec[];
  convergence: { results: ConvergenceResult[]; damping: number } | null;
  busy: boolean;
  onRun: (damping: number) => void;
}

export function ConvergencePanel({ integrators, convergence, busy, onRun }: ConvergenceProps) {
  const [damping, setDamping] = useState(0);
  const expected = (id: string) => integrators.find((i) => i.id === id)?.order ?? 0;

  // Convergence chart: dt ascending on a log axis. Each integrator is a series
  // of measured points; each gets a dashed reference line of its theoretical
  // slope, anchored at its coarsest point, so the eye can check the fit.
  const conv = useMemo(() => {
    if (!convergence) return null;
    const dts = [...convergence.results[0].points.map((p) => p.dt)].sort((a, b) => a - b);
    const cols: (number | null)[][] = [dts];
    const series = [];
    for (const r of convergence.results) {
      const byDt = new Map(r.points.map((p) => [p.dt, p.error > 1e-14 ? p.error : null]));
      cols.push(dts.map((dt) => byDt.get(dt) ?? null));
      series.push({ label: nameOf(integrators, r.integrator), color: colorOf(r.integrator), points: true, width: 2 });
    }
    for (const r of convergence.results) {
      const coarse = r.points[0];
      const q = expected(r.integrator);
      cols.push(dts.map((dt) => {
        const e = coarse.error * Math.pow(dt / coarse.dt, q);
        return e > 1e-14 ? e : null;
      }));
      const short: Record<string, string> = { euler: 'Euler', 'symplectic-euler': 'sympl. Euler', verlet: 'Verlet', rk4: 'RK4' };
      series.push({ label: `theory, ${short[r.integrator] ?? r.integrator}`, color: token('--faint'), dash: [3, 4], width: 1 });
    }
    return { data: cols as unknown as uPlot.AlignedData, series };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [convergence, integrators]);

  return (
    <div className="verify-card">
      <h2>Does each integrator converge at its theoretical order?</h2>
      <p className="lede">
        A mass on a spring (m = k = 1, x₀ = 1) is integrated to t = 5.12 s with dt halved seven times and compared
        with its closed-form solution. On a log–log plot, error against dt is a straight line whose slope is the
        order of the method. Dashed lines show the theoretical slopes; where you cannot see them, the measurement lies on top of them.
      </p>
      <div className="controls-row">
        <div className="segmented" role="group" aria-label="Damping">
          <button aria-pressed={damping === 0} onClick={() => setDamping(0)}>Undamped</button>
          <button aria-pressed={damping === 0.3} onClick={() => setDamping(0.3)}>Damped, c = 0.3</button>
        </div>
        <button className="btn btn-primary" disabled={busy} onClick={() => onRun(damping)}>
          {busy ? 'Running…' : 'Run convergence study'}
        </button>
      </div>
      {conv ? (
        <>
          <Chart data={conv.data} series={conv.series} xLabel="time step dt [s]" yLabel="error at t = 5.12 s"
            logX logY height={260} version={convergence ? convergence.results.length + convergence.damping : 0}
            ariaLabel="Global error against time step, log-log" />
          <table className="results">
            <thead>
              <tr><th>Integrator</th><th>Theory</th><th>Measured order</th><th>Error at dt = 0.005</th><th>Force evaluations</th></tr>
            </thead>
            <tbody>
              {convergence!.results.map((r) => {
                const p = r.points.find((q) => Math.abs(q.dt - 0.005) < 1e-12);
                const ok = Math.abs(r.observedOrder - expected(r.integrator)) < 0.15;
                return (
                  <tr key={r.integrator}>
                    <td><span className="swatch" style={{ background: colorOf(r.integrator) }} />{nameOf(integrators, r.integrator)}</td>
                    <td>{expected(r.integrator)}</td>
                    <td className={ok ? 'pass' : 'fail'}>{r.observedOrder.toFixed(3)} {ok ? '✓' : '✗'}</td>
                    <td>{p ? fmt(p.error, 3) : '—'}</td>
                    <td>{p ? p.forceEvaluations.toLocaleString('en-US') : '—'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </>
      ) : (
        <p className="empty">Run the study to compute it in this browser with the WebAssembly engine. It takes about a second.</p>
      )}
    </div>
  );
}

interface DriftProps {
  integrators: IntegratorSpec[];
  drift: { results: DriftResult[]; dt: number; eccentricity: number; orbits: number } | null;
  busy: boolean;
  onRun: (dt: number, orbits: number) => void;
}

export function DriftPanel({ integrators, drift, busy, onRun }: DriftProps) {
  const [driftDt, setDriftDt] = useState(0.01);
  const [orbits, setOrbits] = useState(50);

  const dr = useMemo(() => {
    if (!drift) return null;
    const t = drift.results[0].t;
    const cols: (number | null)[][] = [t];
    for (const r of drift.results) cols.push(t.map((_, i) => {
      const v = Math.abs(r.error[i] ?? NaN);
      return Number.isFinite(v) && v > 1e-16 ? v : null;
    }));
    return {
      data: cols as unknown as uPlot.AlignedData,
      series: drift.results.map((r) => ({ label: nameOf(integrators, r.integrator), color: colorOf(r.integrator) })),
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [drift, integrators]);

  return (
    <div className="verify-card">
      <h2>Does the energy error stay bounded over many orbits?</h2>
      <p className="lede">
        A two-body orbit (eccentricity 0.6) is run for many periods at a fixed dt. Symplectic methods keep the
        energy error bounded; non-symplectic ones drift. Bounded is not the same as small: at dt = 0.01, RK4's error after 50 orbits is
        smaller than Verlet's, but it keeps growing while Verlet's does not.
      </p>
      <div className="controls-row">
        <div className="segmented" role="group" aria-label="Time step">
          {[0.02, 0.01, 0.005].map((v) => (
            <button key={v} aria-pressed={driftDt === v} onClick={() => setDriftDt(v)}>dt = {v}</button>
          ))}
        </div>
        <div className="segmented" role="group" aria-label="Orbits">
          {[20, 50, 200].map((v) => (
            <button key={v} aria-pressed={orbits === v} onClick={() => setOrbits(v)}>{v} orbits</button>
          ))}
        </div>
        <button className="btn btn-primary" disabled={busy} onClick={() => onRun(driftDt, orbits)}>
          {busy ? 'Running…' : 'Run energy study'}
        </button>
      </div>
      {dr ? (
        <Chart data={dr.data} series={dr.series} xLabel="time (G = 1 units)" yLabel="|E − E₀| / |E₀|"
          logY height={260} version={drift ? drift.orbits + drift.dt : 0}
          ariaLabel="Absolute relative energy error over time, log scale" />
      ) : (
        <p className="empty">Run the study to see the four integrators side by side.</p>
      )}
    </div>
  );
}
