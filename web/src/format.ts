// Number formatting for readouts: fixed significant digits, tabular, with a
// true minus sign, and scientific notation only when magnitude demands it.
export function fmt(v: number, digits = 4): string {
  if (!Number.isFinite(v)) return '—';
  if (v === 0) return '0';
  const a = Math.abs(v);
  const s = a >= 1e5 || a < 1e-3 ? v.toExponential(digits - 1).replace('e', '×10^') : v.toPrecision(digits);
  return s.replace(/^-/, '−').replace(/\^\+?(-?)(\d+)$/, (_, sign: string, exp: string) => sup(sign + exp));
}

const SUP: Record<string, string> = { '-': '⁻', '0': '⁰', '1': '¹', '2': '²', '3': '³', '4': '⁴', '5': '⁵', '6': '⁶', '7': '⁷', '8': '⁸', '9': '⁹' };
function sup(s: string) {
  return s.split('').map((c) => SUP[c] ?? c).join('');
}

export function fmtTime(t: number): string {
  return t < 100 ? t.toFixed(3) : t.toFixed(1);
}

/** Compact axis tick label: plain for ordinary magnitudes, a×10ⁿ otherwise. */
export function fmtAxis(v: number): string {
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a >= 1e-3 && a < 1e5) return String(+v.toPrecision(4)).replace(/^-/, '−');
  const exp = Math.floor(Math.log10(a));
  const mant = +(v / Math.pow(10, exp)).toPrecision(2);
  return (mant === 1 ? '' : mant === -1 ? '−' : `${String(mant).replace(/^-/, '−')}×`) + '10' + sup(String(exp));
}
