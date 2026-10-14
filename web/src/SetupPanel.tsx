import type { IntegratorSpec, ParamSpec, ScenarioSpec } from './protocol';

export interface FieldState {
  text: string;
  error: string | null;
}

/** Parses and range-checks a parameter field. Returns an error message or null. */
export function validateParam(p: ParamSpec, text: string): string | null {
  if (text.trim() === '') return 'Enter a value.';
  const v = Number(text);
  if (!Number.isFinite(v)) return 'Enter a number.';
  if (p.integer && !Number.isInteger(v)) return 'Enter a whole number.';
  if (v < p.min || v > p.max) return `Use a value from ${p.min} to ${p.max}${p.unit ? ' ' + p.unit : ''}.`;
  return null;
}

export function validateDt(text: string): string | null {
  const v = Number(text);
  if (text.trim() === '' || !Number.isFinite(v)) return 'Enter a time step in seconds.';
  if (v <= 0) return 'The time step must be greater than 0.';
  if (v > 0.1) return 'Use 0.1 s or less.';
  return null;
}

interface Props {
  scenario: ScenarioSpec;
  integrators: IntegratorSpec[];
  integrator: string;
  onIntegrator: (id: string) => void;
  dt: FieldState;
  onDt: (text: string) => void;
  fields: Record<string, FieldState>;
  onField: (p: ParamSpec, text: string) => void;
  pending: boolean;
  onApply: () => void;
  loadErrors: string[];
  liveErrors: Record<string, string>;
  dimensionless: boolean;
}

export function SetupPanel(props: Props) {
  const { scenario, integrators, integrator, dt, fields, pending, loadErrors, liveErrors, dimensionless } = props;
  const anyInvalid = Object.values(fields).some((f) => f.error) || !!dt.error;
  return (
    <aside className="panel setup" aria-label="Simulation setup">
      <section className="block">
        <h2>{scenario.title}</h2>
        <p className="lede">{scenario.summary}</p>
        <p className="units">Units: {scenario.units}</p>
      </section>

      {loadErrors.length > 0 && (
        <div className="notice notice-error" role="alert">
          <strong>These settings can't be loaded.</strong>
          <ul>{loadErrors.map((e) => <li key={e}>{e}</li>)}</ul>
        </div>
      )}

      <section className="block">
        <h3 id="integrator-label">Integrator</h3>
        <div className="integrators" role="radiogroup" aria-labelledby="integrator-label">
          {integrators.map((i) => (
            <label key={i.id} className={`integrator ${i.id === integrator ? 'is-on' : ''}`}>
              <input
                type="radio"
                name="integrator"
                value={i.id}
                checked={i.id === integrator}
                onChange={() => props.onIntegrator(i.id)}
              />
              <span className="integrator-name">{i.name}</span>
              <span className="integrator-meta">
                order {i.order}{i.symplectic ? ', symplectic' : ''}
              </span>
            </label>
          ))}
        </div>
        <div className="field">
          <label htmlFor="dt">Time step dt <span className="unit">{dimensionless ? '' : 's'}</span></label>
          <input
            id="dt"
            inputMode="decimal"
            value={dt.text}
            aria-invalid={!!dt.error}
            aria-describedby={dt.error ? 'dt-error' : 'dt-help'}
            onChange={(e) => props.onDt(e.target.value)}
          />
          {dt.error
            ? <p className="field-error" id="dt-error">{dt.error}</p>
            : <p className="field-help" id="dt-help">Applies immediately. Smaller is more accurate and slower.</p>}
        </div>
      </section>

      <section className="block">
        <h3>Parameters</h3>
        {scenario.params.map((p) => {
          const f = fields[p.key] ?? { text: String(p.default), error: null };
          const id = `param-${p.key}`;
          const err = f.error ?? liveErrors[p.key] ?? null;
          const numeric = Number(f.text);
          return (
            <div className="field" key={p.key}>
              <label htmlFor={id}>
                {p.label} {p.unit && <span className="unit">{p.unit}</span>}
                {!p.live && <span className="applies"> on reset</span>}
              </label>
              <div className="field-row">
                <input
                  type="range"
                  aria-label={`${p.label} slider`}
                  min={p.min}
                  max={p.max}
                  step={p.step || 'any'}
                  value={Number.isFinite(numeric) ? Math.min(p.max, Math.max(p.min, numeric)) : p.default}
                  onChange={(e) => props.onField(p, e.target.value)}
                />
                <input
                  id={id}
                  className="num"
                  inputMode="decimal"
                  value={f.text}
                  aria-invalid={!!err}
                  aria-describedby={err ? `${id}-error` : undefined}
                  onChange={(e) => props.onField(p, e.target.value)}
                />
              </div>
              {err ? <p className="field-error" id={`${id}-error`}>{err}</p> : <p className="field-help">{p.help}</p>}
            </div>
          );
        })}
        <button className="btn btn-secondary wide" disabled={!pending || anyInvalid} onClick={props.onApply}>
          {pending ? 'Apply and restart' : 'Settings applied'}
        </button>
      </section>
    </aside>
  );
}
