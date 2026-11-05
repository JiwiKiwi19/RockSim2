import { useEffect, useRef } from 'react';
import type { ReactNode } from 'react';
import type { Desk } from './useWindows';

/** The dithered desktop that windows sit on. Reports its measured size. */
export function Desktop({ onSize, mobile, children }: { onSize: (d: Desk) => void; mobile: boolean; children: ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const measure = () => { onSize({ w: el.clientWidth, h: el.clientHeight }); };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => ro.disconnect();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <main className={`desktop ${mobile ? 'is-stacked' : ''}`} ref={ref}>
      {children}
    </main>
  );
}
