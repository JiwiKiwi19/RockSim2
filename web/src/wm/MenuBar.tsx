import { useEffect, useRef, useState } from 'react';

export type MenuItem =
  | { separator: true }
  | {
      label: string;
      onSelect: () => void;
      kind?: 'item' | 'radio' | 'check';
      checked?: boolean;
      disabled?: boolean;
      shortcut?: string;
    };

export interface Menu { label: string; items: MenuItem[] }

const isAction = (i: MenuItem): i is Exclude<MenuItem, { separator: true }> => !('separator' in i);

/** A menu bar with the keyboard model of the ARIA menubar pattern. */
export function MenuBar({ menus }: { menus: Menu[] }) {
  const [open, setOpen] = useState<number | null>(null);
  const [active, setActive] = useState(0);
  const barRef = useRef<HTMLElement>(null);
  const triggers = useRef<(HTMLButtonElement | null)[]>([]);
  const itemRefs = useRef<(HTMLButtonElement | null)[]>([]);

  useEffect(() => {
    if (open === null) return;
    const away = (e: PointerEvent) => {
      if (!barRef.current?.contains(e.target as Node)) setOpen(null);
    };
    document.addEventListener('pointerdown', away);
    return () => document.removeEventListener('pointerdown', away);
  }, [open]);

  useEffect(() => {
    if (open !== null) itemRefs.current[active]?.focus();
  }, [open, active]);

  const actionsOf = (m: number) => menus[m].items.map((it, i) => (isAction(it) && !it.disabled ? i : -1)).filter((i) => i >= 0);

  const openMenu = (m: number, first = true) => {
    const a = actionsOf(m);
    setOpen(m);
    setActive(first ? a[0] ?? 0 : a[a.length - 1] ?? 0);
  };
  const close = (refocus: boolean) => {
    const m = open;
    setOpen(null);
    if (refocus && m !== null) triggers.current[m]?.focus();
  };
  const stepItem = (m: number, dir: 1 | -1) => {
    const a = actionsOf(m);
    if (!a.length) return;
    const at = a.indexOf(active);
    setActive(a[(at + dir + a.length) % a.length]);
  };

  const onMenuKey = (e: React.KeyboardEvent, m: number) => {
    const n = menus.length;
    switch (e.key) {
      case 'ArrowDown': e.preventDefault(); stepItem(m, 1); break;
      case 'ArrowUp': e.preventDefault(); stepItem(m, -1); break;
      case 'Home': e.preventDefault(); setActive(actionsOf(m)[0] ?? 0); break;
      case 'End': e.preventDefault(); setActive(actionsOf(m).slice(-1)[0] ?? 0); break;
      case 'ArrowRight': e.preventDefault(); openMenu((m + 1) % n); break;
      case 'ArrowLeft': e.preventDefault(); openMenu((m - 1 + n) % n); break;
      case 'Escape': e.preventDefault(); close(true); break;
      case 'Tab': setOpen(null); break;
    }
  };
  const onTriggerKey = (e: React.KeyboardEvent, m: number) => {
    const n = menus.length;
    if (e.key === 'ArrowUp' || e.key === 'ArrowDown') { e.preventDefault(); openMenu(m, e.key === 'ArrowDown'); }
    else if (e.key === 'ArrowRight') { e.preventDefault(); triggers.current[(m + 1) % n]?.focus(); }
    else if (e.key === 'ArrowLeft') { e.preventDefault(); triggers.current[(m - 1 + n) % n]?.focus(); }
  };

  return (
    <nav className="menubar" ref={barRef} aria-label="Main menu">
      {menus.map((menu, m) => (
        <div className="menu" key={menu.label}>
          <button
            ref={(el) => { triggers.current[m] = el; }}
            className="menu-trigger"
            aria-haspopup="menu"
            aria-expanded={open === m}
            aria-label={`${menu.label} menu`}
            onClick={() => (open === m ? close(false) : openMenu(m))}
            onPointerEnter={() => { if (open !== null && open !== m) openMenu(m); }}
            onKeyDown={(e) => onTriggerKey(e, m)}
          >
            {menu.label}
          </button>
          {open === m && (
            <ul className="menu-list" role="menu" aria-label={menu.label} onKeyDown={(e) => onMenuKey(e, m)}>
              {menu.items.map((it, i) =>
                isAction(it) ? (
                  <li role="none" key={it.label}>
                    <button
                      ref={(el) => { itemRefs.current[i] = el; }}
                      role={it.kind === 'radio' ? 'menuitemradio' : it.kind === 'check' ? 'menuitemcheckbox' : 'menuitem'}
                      aria-checked={it.kind ? !!it.checked : undefined}
                      aria-disabled={it.disabled || undefined}
                      tabIndex={i === active ? 0 : -1}
                      className="menu-item"
                      onClick={() => {
                        if (it.disabled) return;
                        close(true);
                        it.onSelect();
                      }}
                    >
                      <span className="menu-mark" aria-hidden="true">{it.kind && it.checked ? '■' : ''}</span>
                      <span className="menu-label">{it.label}</span>
                      {it.shortcut && <kbd>{it.shortcut}</kbd>}
                    </button>
                  </li>
                ) : (
                  <li role="separator" key={`sep-${i}`} className="menu-sep" />
                ),
              )}
            </ul>
          )}
        </div>
      ))}
    </nav>
  );
}
