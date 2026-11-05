import { useRef, useState } from 'react';
import type { IntegratorSpec, ParamSpec } from './protocol';

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

const SCRUB_PX = 3;
const show = (v: number) => String(+v.toPrecision(6));

interface Props {
  integrators: IntegratorSpec[];
  params: ParamSpec[];
  integrator: string;
  onIntegrator: (id: string) => void;
  dt: FieldState;
  onDt: (text: string) => void;
  fields: Record<string, FieldState>;
  onField: (p: ParamSpec, text: string) => void;
  /** What the engine is running now: shown for live parameters, which the timeline can change. */
  live: Record<string, number> | undefined;
  pending: boolean;
  onApply: () => void;
  loadErrors: string[];
  liveErrors: Record<string, string>;
  dimensionless: boolean;
  /** "A" or "B" when a branch exists: which run these controls edit. */
  runLabel: string | null;
}

export function ParametersPanel(props: Props) {
  const { integrators, params, integrator, dt, fields, live, pending, loadErrors, liveErrors, dimensionless } = props;
  const anyInvalid = Object.values(fields).some((f) => f.error) || !!dt.error;
  const [editing, setEditing] = useState<string | null>(null);
  const scrub = useRef<{ x: number; v: number; active: boolean } | null>(null);

  return (
    <div className="params">
      {props.runLabel && <p className="notice">These controls change run {props.runLabel}.</p>}
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
        <p className="field-help">Drag across a number to scrub it.</p>
        {params.map((p) => {
          const f = fields[p.key] ?? { text: String(p.default), error: null };
          const id = `param-${p.key}`;
          const err = f.error ?? liveErrors[p.key] ?? null;
          const fromEngine = p.live && live?.[p.key] !== undefined && editing !== p.key;
          const text = fromEngine ? show(live![p.key]) : f.text;
          const numeric = Number(text);
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
                  value={text}
                  aria-invalid={!!err}
                  aria-describedby={err ? `${id}-error` : undefined}
                  onFocus={() => setEditing(p.key)}
                  onBlur={() => setEditing(null)}
                  onChange={(e) => props.onField(p, e.target.value)}
                  onPointerDown={(e) => {
                    if (e.pointerType === 'mouse' && e.button !== 0) return;
                    scrub.current = { x: e.clientX, v: Number.isFinite(numeric) ? numeric : p.default, active: false };
                    e.currentTarget.setPointerCapture(e.pointerId);
                  }}
                  onPointerMove={(e) => {
                    const s = scrub.current;
                    if (!s) return;
                    const dx = e.clientX - s.x;
                    if (!s.active && Math.abs(dx) < SCRUB_PX) return;
                    if (!s.active) { s.active = true; setEditing(p.key); }
                    const per = (p.max - p.min) / 300;
                    let v = s.v + dx * per;
                    if (p.step) v = Math.round(v / p.step) * p.step;
                    if (p.integer) v = Math.round(v);
                    v = Math.min(p.max, Math.max(p.min, v));
                    props.onField(p, show(v));
                  }}
                  onPointerUp={(e) => {
                    const s = scrub.current;
                    scrub.current = null;
                    if (s?.active) { setEditing(null); e.currentTarget.blur(); }
                  }}
                  onPointerCancel={() => { scrub.current = null; }}
                />
              </div>
              {err ? <p className="field-error" id={`${id}-error`}>{err}</p> : <p className="field-help">{p.help}</p>}
            </div>
          );
        })}
        <button className="btn wide" disabled={!pending || anyInvalid} onClick={props.onApply}>
          {pending ? 'Apply and restart' : 'Settings applied'}
        </button>
      </section>
    </div>
  );
}
