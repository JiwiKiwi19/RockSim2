import { useRef } from 'react';
import type { ReactNode } from 'react';
import { MIN_H, MIN_W, TITLE_HEIGHT } from './useWindows';
import type { Rect, WinState } from './useWindows';

interface Props {
  title: string;
  state: WinState;
  mobile: boolean;
  className?: string;
  onPlace: (r: Rect) => void;
  onRaise: () => void;
  onCollapse: () => void;
  onClose: () => void;
  children: ReactNode;
}

type Drag = { kind: 'move' | 'size'; px: number; py: number; start: Rect };

/** A draggable, resizable, collapsible window with a striped title bar. */
export function Window({ title, state, mobile, className, onPlace, onRaise, onCollapse, onClose, children }: Props) {
  const drag = useRef<Drag | null>(null);
  const rect: Rect = { x: state.x, y: state.y, w: state.w, h: state.h };

  const begin = (kind: Drag['kind']) => (e: React.PointerEvent) => {
    if (mobile || e.button !== 0) return;
    drag.current = { kind, px: e.clientX, py: e.clientY, start: rect };
    e.currentTarget.setPointerCapture(e.pointerId);
    e.preventDefault();
  };
  const move = (e: React.PointerEvent) => {
    const d = drag.current;
    if (!d) return;
    const dx = e.clientX - d.px, dy = e.clientY - d.py;
    onPlace(d.kind === 'move'
      ? { ...d.start, x: d.start.x + dx, y: d.start.y + dy }
      : { ...d.start, w: Math.max(MIN_W, d.start.w + dx), h: Math.max(MIN_H, d.start.h + dy) });
  };
  const end = () => { drag.current = null; };

  const onKey = (e: React.KeyboardEvent) => {
    if (mobile || e.target !== e.currentTarget) return;
    const step = e.shiftKey ? 48 : 16;
    const dx = e.key === 'ArrowLeft' ? -step : e.key === 'ArrowRight' ? step : 0;
    const dy = e.key === 'ArrowUp' ? -step : e.key === 'ArrowDown' ? step : 0;
    if (dx || dy) {
      e.preventDefault();
      onPlace({ ...rect, x: rect.x + dx, y: rect.y + dy });
    }
  };

  const style: React.CSSProperties = mobile
    ? {}
    : { left: state.x, top: state.y, width: state.w, height: state.collapsed ? TITLE_HEIGHT : state.h, zIndex: state.z };

  return (
    <section
      className={`win ${state.collapsed ? 'is-collapsed' : ''} ${className ?? ''}`}
      style={style}
      aria-label={title}
      onPointerDownCapture={onRaise}
    >
      <header
        className="win-title"
        tabIndex={mobile ? undefined : 0}
        aria-label={mobile ? undefined : `${title} title bar. Arrow keys move the window.`}
        onPointerDown={begin('move')}
        onPointerMove={move}
        onPointerUp={end}
        onPointerCancel={end}
        onKeyDown={onKey}
      >
        <button className="win-box" aria-label={`Close ${title}`} onPointerDown={(e) => e.stopPropagation()} onClick={onClose}>
          <span aria-hidden="true" />
        </button>
        <span className="win-name">{title}</span>
        <button
          className="win-box win-box-collapse"
          aria-label={`${state.collapsed ? 'Expand' : 'Collapse'} ${title}`}
          aria-expanded={!state.collapsed}
          onPointerDown={(e) => e.stopPropagation()}
          onClick={onCollapse}
        >
          <span aria-hidden="true" />
        </button>
      </header>
      <div className="win-body" hidden={state.collapsed}>{children}</div>
      {!mobile && !state.collapsed && (
        <div className="win-grip" aria-hidden="true" onPointerDown={begin('size')} onPointerMove={move} onPointerUp={end} onPointerCancel={end} />
      )}
    </section>
  );
}
