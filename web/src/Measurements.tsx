import { Chart } from './Chart';
import { fmt, fmtTime } from './format';
import type { History } from './history';
import type { Frame, ScenarioSpec, Topology } from './protocol';

interface Props {
  scenario: ScenarioSpec;
  frame: Frame | null;
  topology: Topology | null;
  history: History;
  version: number;
  selected: number | null;
  onClearSelection: () => void;
  dimensionless: boolean;
}

const ENERGY_SERIES = [
  { label: 'Kinetic', color: '#1F4FD8' },
  { label: 'Potential', color: '#A86A12' },
  { label: 'Total', color: '#1B2430', width: 2.25 },
];
const ERROR_SERIES = [{ label: '(E − E₀) / scale', color: '#D23A4B' }];
const OSC_SERIES = [
  { label: 'Numerical', color: '#1F4FD8', width: 2 },
  { label: 'Exact', color: '#D23A4B', dash: [5, 4] },
];

export function Measurements({ scenario, frame, topology, history, version, selected, onClearSelection, dimensionless }: Props) {
  const d = frame?.diag;
  const u = (si: string) => (dimensionless ? '' : si);
  const p = d ? Math.hypot(d.now.px, d.now.py) : NaN;
  const realTime = frame && topology ? frame.stepsPerSecond * topology.meta.dt : 0;
  const isOsc = scenario.id === 'spring-oscillator';
  const period = topology?.meta.referencePeriod ?? 0;

  return (
    <aside className="panel measure" aria-label="Measurements">
      <section className="block">
        <h2>Measurements</h2>
        <dl className="readouts">
          <div><dt>Time</dt><dd>{d ? fmtTime(d.time) : '—'}<span className="unit">{u(' s')}</span></dd></div>
          <div><dt>Steps</dt><dd>{d ? d.steps.toLocaleString('en-US') : '—'}</dd></div>
          <div><dt>Total energy</dt><dd>{d ? fmt(d.now.total) : '—'}<span className="unit">{u(' J')}</span></dd></div>
          <div className={d && d.relativeEnergyError > 0.01 ? 'warn' : ''}
            title="(E − E₀) divided by the largest of |E₀|, |K₀|, |U₀|. Dampers and inelastic contacts lower it; a rise means numerical error.">
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
          <div><dt>Engine rate</dt><dd>{frame ? Math.round(frame.stepsPerSecond).toLocaleString('en-US') : '—'}<span className="unit"> steps/s</span></dd></div>
          <div><dt>Real-time factor</dt><dd>{frame?.running ? `${realTime.toFixed(2)}×` : 'paused'}</dd></div>
        </dl>
        <p className="what-to-expect">{scenario.validation}</p>
      </section>

      {isOsc && (
        <section className="block">
          <h3>Position against the exact solution</h3>
          <Chart
            data={[history.t, history.numX, history.refX]}
            series={OSC_SERIES}
            xLabel={`time${u(' [s]')}`}
            yLabel={`x${u(' [m]')}`}
            version={version}
            ariaLabel="Numerical and exact position over time"
          />
        </section>
      )}

      <section className="block">
        <h3>Energy</h3>
        <Chart
          data={[history.t, history.K, history.U, history.E]}
          series={ENERGY_SERIES}
          xLabel={`time${u(' [s]')}`}
          yLabel={`energy${u(' [J]')}`}
          version={version}
          ariaLabel="Kinetic, potential and total energy over time"
        />
      </section>

      <section className="block">
        <h3>Relative energy change</h3>
        <Chart
          data={[history.t, history.rel]}
          series={ERROR_SERIES}
          xLabel={`time${u(' [s]')}`}
          yLabel="relative error"
          height={140}
          version={version}
          ariaLabel="Relative energy error over time"
        />
      </section>

      <section className="block inspector">
        <h3>Inspector</h3>
        {selected === null || !frame || !topology || selected >= frame.x.length ? (
          <p className="empty">Click a body in the view to read its state.</p>
        ) : (
          <>
            <dl className="readouts">
              <div><dt>Body</dt><dd>#{selected}{topology.fixed[selected] ? ' (anchor)' : ''}</dd></div>
              <div><dt>Mass</dt><dd>{fmt(topology.mass[selected])}<span className="unit">{u(' kg')}</span></dd></div>
              <div><dt>Position</dt><dd>({fmt(frame.x[selected], 3)}, {fmt(frame.y[selected], 3)})<span className="unit">{u(' m')}</span></dd></div>
              <div><dt>Velocity</dt><dd>({fmt(frame.vx[selected], 3)}, {fmt(frame.vy[selected], 3)})<span className="unit">{u(' m/s')}</span></dd></div>
              <div><dt>Speed</dt><dd>{fmt(Math.hypot(frame.vx[selected], frame.vy[selected]), 4)}<span className="unit">{u(' m/s')}</span></dd></div>
              <div><dt>Radius</dt><dd>{fmt(topology.radius[selected], 3)}<span className="unit">{u(' m')}</span></dd></div>
            </dl>
            <button className="btn btn-quiet" onClick={onClearSelection}>Clear selection</button>
          </>
        )}
      </section>

      <section className="block hash">
        <h3>Reproducibility</h3>
        <p>
          State hash <code>{frame?.hash ?? '—'}</code>. The same scenario, seed, integrator, dt and step count
          give this exact hash natively and in this browser.
        </p>
      </section>
    </aside>
  );
}
