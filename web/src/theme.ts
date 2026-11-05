// Canvas and chart colours come from the CSS custom properties in styles.css,
// so the palette is defined in one place.
const cache = new Map<string, string>();

export function token(name: string): string {
  let v = cache.get(name);
  if (!v) {
    v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    if (v) cache.set(name, v);
  }
  return v || '#000000';
}

/** `#rrggbb` plus an alpha in [0, 1] as an rgba() string. */
export function alpha(hex: string, a: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
}
