import uPlot from 'uplot';
import { Chart } from './Chart';
import { fmt, fmtTime } from './format';
import type { Frame, ScenarioSpec, SeriesData, Topology } from './protocol';
import { token } from './theme';

interface ChartProps {
  series: SeriesData[] | null;
  frame: Frame | null;
  version: number;
  onSeek: (time: number) => void;
  dimensionless: boolean;
}

const unit = (dimensionless: boolean) => (si: string) => (dimensionless ? '' : si);

function eventTimes(frame: Frame | null, run: number): number[] {
  const flat = frame?.runs[run]?.events;
  const out: number[] = [];
  if (flat) for (let i = 1; i < flat.length; i += 3) out.push(flat[i]);
  return out;
}

/** Plots one or two runs on a shared time axis (a join when B has its own samples). */
function joined(series: SeriesData[], cols: (keyof SeriesData)[][]): uPlot.AlignedData {
  if (series.length < 2 || cols.length < 2) return [series[0].t, ...cols[0].map((c) => series[0][c])] as uPlot.AlignedData;
  return uPlot.join(series.slice(0, 2).map((s, i) => [s.t, ...cols[i].map((c) => s[c])] as uPlot.AlignedData));
}

export function MeasurementsPanel({ frame, topology, dimensionless }: {
  frame: Frame | null; topology: Topology | null; dimensionless: boolean;
}) {
  const a = frame?.runs[0];
  const b = frame?.runs[1];
  const d = a?.diag;
  const u = unit(dimensionless);
  const p = d ? Math.hypot(d.now.px, d.now.py) : NaN;
  const sps = frame?.timeline.stepsPerSecond ?? 0;
  const running = frame?.timeline.running ?? false;
  const realTime = d ? sps * d.dt : 0;
  const period = topology?.meta.referencePeriod ?? 0;
  const events = d?.eventCount ?? 0;

  return (
    <div>
      <section className="block">
        <dl className="readouts">
          <div><dt>Time</dt><dd>{d ? fmtTime(d.time) : '—'}<span className="unit">{u(' s')}</span></dd></div>
          <div><dt>Steps</dt><dd>{d ? d.steps.toLocaleString('en-US') : '—'}</dd></div>
          <div><dt>Total energy</dt><dd>{d ? fmt(d.now.total) : '—'}<span className="unit">{u(' J')}</span></dd></div>
          <div className={d && d.relativeEnergyError > 0.01 ? 'warn' : ''}
            title="(E − E₀) divided by the largest of |E₀|, |K₀|, |U₀|. Dampers and inelastic contacts lower it; a rise means numerical error, or work done by your hand.">
            <dt>Energy change</dt><dd>{d ? fmt(d.relativeEnergyError, 3) : '—'}</dd>
          </div>
          <div><dt>Momentum |p|</dt><dd>{d ? fmt(p, 3) : '—'}<span className="unit">{u(' kg·m/s')}</span></dd></div>
          <div><dt>Angular momentum</dt><dd>{d ? fmt(d.now.angularMomentum, 4) : '—'}<span className="unit">{u(' kg·m²/s')}</span></dd></div>
          {topology?.meta.scenario === 'particle-box' && (
            <div><dt>Contacts this step</dt><dd>{d ? d.contacts : '—'}</dd></div>
          )}
          {period > 0 && (
            <div><dt>Exact period</dt><dd>{fmt(period, 6)}<span className="unit">{u(' s')}</span></dd></div>
          )}
          <div><dt>Engine rate</dt><dd>{running ? Math.round(sps).toLocaleString('en-US') : '—'}<span className="unit"> steps/s</span></dd></div>
          <div><dt>Real-time factor</dt><dd>{running ? `${realTime.toFixed(2)}×` : 'paused'}</dd></div>
          {b && <div><dt>Run B energy</dt><dd>{fmt(b.diag.now.total)}<span className="unit">{u(' J')}</span></dd></div>}
          {b && <div><dt>A–B divergence</dt><dd>{fmt(frame!.timeline.divergence, 3)}<span className="unit">{u(' m')}</span></dd></div>}
        </dl>
      </section>

      <section className="block hash">
        <h3>Reproducibility</h3>
        {events === 0 ? (
          <p>
            State hash <code>{a?.hash ?? '—'}</code>. The same scenario, seed, integrator, dt and step count
            give this exact hash natively and in this browser.
          </p>
        ) : (
          <p>
            State hash <code>{a?.hash ?? '—'}</code>. This run includes {events} recorded action{events === 1 ? '' : 's'} (grabs,
            drags, edits), so the hash applies to this run with its actions replayed at the same steps, not to the bare scenario.
          </p>
        )}
        {b && <p>Run B hash <code className="hash-b">{b.hash}</code></p>}
      </section>
    </div>
  );
}

export function InspectorPanel({ frame, topology, selected, run, onClear, dimensionless }: {
  frame: Frame | null; topology: Topology | null; selected: number | null; run: number; onClear: () => void; dimensionless: boolean;
}) {
  const rf = frame?.runs[run];
  const u = unit(dimensionless);
  if (selected === null || !rf || !topology || selected >= rf.x.length) {
    return <p className="empty">Click a body in the view to read its state.</p>;
  }
  return (
    <div className="block">
      <dl className="readouts">
        <div><dt>Body</dt><dd>#{selected}{topology.fixed[selected] ? ' (anchor)' : ''}</dd></div>
        <div><dt>Mass</dt><dd>{fmt(topology.mass[selected])}<span className="unit">{u(' kg')}</span></dd></div>
        <div><dt>Position</dt><dd>({fmt(rf.x[selected], 3)}, {fmt(rf.y[selected], 3)})<span className="unit">{u(' m')}</span></dd></div>
        <div><dt>Velocity</dt><dd>({fmt(rf.vx[selected], 3)}, {fmt(rf.vy[selected], 3)})<span className="unit">{u(' m/s')}</span></dd></div>
        <div><dt>Speed</dt><dd>{fmt(Math.hypot(rf.vx[selected], rf.vy[selected]), 4)}<span className="unit">{u(' m/s')}</span></dd></div>
        <div><dt>Radius</dt><dd>{fmt(topology.radius[selected], 3)}<span className="unit">{u(' m')}</span></dd></div>
      </dl>
      <p className="field-help">The blue arrow on the body is its velocity. While paused, drag its tip to change the velocity.</p>
      <button className="btn btn-quiet" onClick={onClear}>Clear selection</button>
    </div>
  );
}

export function EnergyPanel({ series, frame, version, onSeek, dimensionless }: ChartProps) {
  const u = unit(dimensionless);
  if (!series?.[0]) return null;
  const two = series.length > 1;
  const spec = [
    { label: 'Kinetic', color: token('--blue') },
    { label: 'Potential', color: token('--amber') },
    { label: two ? 'Total A' : 'Total', color: token('--ink'), width: 3 },
    ...(two ? [{ label: 'Total B', color: token('--ink'), dash: [6, 4], width: 3 }] : []),
  ];
  const data = joined(series, two ? [['K', 'U', 'E'], ['E']] : [['K', 'U', 'E']]);
  return (
    <Chart
      data={data} series={spec} xLabel={`time${u(' [s]')}`} yLabel={`energy${u(' [J]')}`} version={version}
      ariaLabel="Kinetic, potential and total energy over time. Click to seek."
      cursor={frame?.timeline.cursorTime} markers={eventTimes(frame, 0)} onSeek={onSeek} height={190}
    />
  );
}

export function ErrorPanel({ series, frame, version, onSeek }: ChartProps) {
  if (!series?.[0]) return null;
  const two = series.length > 1;
  const spec = [
    { label: two ? '(E − E₀) / scale, A' : '(E − E₀) / scale', color: token('--red') },
    ...(two ? [{ label: 'B', color: token('--red'), dash: [6, 4] }] : []),
  ];
  return (
    <Chart
      data={joined(series, two ? [['rel'], ['rel']] : [['rel']])} series={spec} xLabel="time [s]" yLabel="relative error" height={160}
      version={version} ariaLabel="Relative energy error over time. Click to seek."
      cursor={frame?.timeline.cursorTime} markers={eventTimes(frame, 0)} onSeek={onSeek}
    />
  );
}

export function OscillatorPanel({ series, frame, version, onSeek, dimensionless }: ChartProps) {
  const u = unit(dimensionless);
  if (!series?.[0]) return null;
  const spec = [
    { label: 'Numerical', color: token('--blue'), width: 3 },
    { label: 'Exact', color: token('--red'), dash: [5, 4] },
  ];
  return (
    <Chart
      data={joined(series.slice(0, 1), [['numX', 'refX']])} series={spec} xLabel={`time${u(' [s]')}`} yLabel={`x${u(' [m]')}`}
      version={version} ariaLabel="Numerical and exact position over time. Click to seek."
      cursor={frame?.timeline.cursorTime} markers={eventTimes(frame, 0)} onSeek={onSeek} height={190}
    />
  );
}

export function DivergencePanel({ series, frame, version, onSeek, dimensionless }: ChartProps) {
  const u = unit(dimensionless);
  if (!series?.[1]) return <p className="empty">Edit the past to start a second run, B, and compare it with A here.</p>;
  return (
    <Chart
      data={[series[1].t, series[1].div]} series={[{ label: 'RMS position difference A–B', color: token('--ink'), width: 3 }]}
      xLabel={`time${u(' [s]')}`} yLabel={`divergence${u(' [m]')}`} version={version} height={170}
      ariaLabel="Root-mean-square position difference between runs A and B over time. Click to seek."
      cursor={frame?.timeline.cursorTime} markers={eventTimes(frame, 1)} onSeek={onSeek}
    />
  );
}

export function NotesPanel({ scenario }: { scenario: ScenarioSpec }) {
  return (
    <div className="notes">
      <p>{scenario.summary}</p>
      <p className="units">Units: {scenario.units}</p>
      <h3>What to expect</h3>
      <p className="what-to-expect">{scenario.validation}</p>
      <h3>Try this</h3>
      <ol className="try-this">
        {scenario.tryThis.map((t) => <li key={t}>{t}</li>)}
      </ol>
    </div>
  );
}
