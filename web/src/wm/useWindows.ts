import { useCallback, useEffect, useState } from 'react';

export interface Rect { x: number; y: number; w: number; h: number }
export interface Desk { w: number; h: number }

export interface WinDef {
  id: string;
  title: string;
  /** Open on first visit and after Reset Layout. */
  defaultOpen: boolean;
  /** Default position and size for a desktop of the given size. */
  rect: (desk: Desk) => Rect;
}

export interface WinState extends Rect {
  open: boolean;
  collapsed: boolean;
  z: number;
}

type Layout = Record<string, WinState>;

export const MOBILE_WIDTH = 760;
export const TITLE_HEIGHT = 28;
export const MIN_W = 200;
export const MIN_H = 120;

// Bump when the layout format or the default arrangement changes incompatibly.
const KEY = 'rocksim.layout.v1';

/** Keeps a window fully inside the desktop, shrinking it if it is larger. */
export function clampRect(r: Rect, desk: Desk): Rect {
  const w = Math.min(Math.max(r.w, MIN_W), Math.max(desk.w, MIN_W));
  const h = Math.min(Math.max(r.h, TITLE_HEIGHT), Math.max(desk.h, TITLE_HEIGHT));
  return {
    w, h,
    x: Math.min(Math.max(r.x, 0), Math.max(0, desk.w - w)),
    y: Math.min(Math.max(r.y, 0), Math.max(0, desk.h - h)),
  };
}

function defaults(defs: WinDef[], desk: Desk): Layout {
  const out: Layout = {};
  defs.forEach((d, i) => {
    out[d.id] = { ...clampRect(d.rect(desk), desk), open: d.defaultOpen, collapsed: false, z: i + 1 };
  });
  return out;
}

function load(defs: WinDef[], desk: Desk): Layout {
  const fresh = defaults(defs, desk);
  try {
    const saved = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Layout | null;
    if (!saved || typeof saved !== 'object') return fresh;
    for (const d of defs) {
      const s = saved[d.id];
      const ok = s && [s.x, s.y, s.w, s.h, s.z].every(Number.isFinite) && typeof s.open === 'boolean' && typeof s.collapsed === 'boolean';
      if (ok) fresh[d.id] = { ...clampRect(s, desk), open: s.open, collapsed: s.collapsed, z: s.z };
    }
  } catch {
    // Unreadable storage: start from the defaults.
  }
  return fresh;
}

/**
 * Window positions, sizes, stacking, collapse and visibility, saved to
 * localStorage. Waits for a measured desktop before laying anything out, and
 * re-clamps every window when the desktop shrinks so none is stranded.
 */
export function useWindows(defs: WinDef[], desk: Desk) {
  const [layout, setLayout] = useState<Layout | null>(null);
  const ready = desk.w > 0 && desk.h > 0;
  const mobile = desk.w > 0 && desk.w < MOBILE_WIDTH;

  useEffect(() => {
    if (!ready) return;
    setLayout((cur) => {
      if (!cur) return load(defs, desk);
      const next: Layout = {};
      let changed = false;
      for (const d of defs) {
        const s = cur[d.id] ?? { ...clampRect(d.rect(desk), desk), open: d.defaultOpen, collapsed: false, z: 1 };
        const r = mobile ? s : clampRect(s, desk);
        if (r.x !== s.x || r.y !== s.y || r.w !== s.w || r.h !== s.h || !cur[d.id]) changed = true;
        next[d.id] = { ...s, x: r.x, y: r.y, w: r.w, h: r.h };
      }
      return changed ? next : cur;
    });
    // defs are static apart from titles.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ready, desk.w, desk.h, mobile]);

  useEffect(() => {
    if (layout) {
      try { localStorage.setItem(KEY, JSON.stringify(layout)); } catch { /* private mode: layout just will not persist */ }
    }
  }, [layout]);

  const patch = useCallback((id: string, p: Partial<WinState>) => {
    setLayout((cur) => (cur && cur[id] ? { ...cur, [id]: { ...cur[id], ...p } } : cur));
  }, []);

  const raise = useCallback((id: string) => {
    setLayout((cur) => {
      if (!cur || !cur[id]) return cur;
      const top = Math.max(...Object.values(cur).map((w) => w.z));
      return cur[id].z === top ? cur : { ...cur, [id]: { ...cur[id], z: top + 1 } };
    });
  }, []);

  const place = useCallback((id: string, r: Rect) => patch(id, clampRect(r, desk)), [patch, desk]);

  const setOpen = useCallback((id: string, open: boolean) => {
    patch(id, open ? { open, collapsed: false } : { open });
    if (open) raise(id);
  }, [patch, raise]);

  const reset = useCallback(() => setLayout(defaults(defs, desk)), [defs, desk]);

  return { layout, mobile, place, raise, setOpen, patch, reset };
}
