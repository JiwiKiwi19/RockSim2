import { useEffect, useRef } from 'react';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { fmtAxis } from './format';
import { token } from './theme';

export interface SeriesSpec {
  label: string;
  color: string;
  dash?: number[];
  width?: number;
  points?: boolean;
}

interface Props {
  /** Columns: x first, then one array per series. */
  data: uPlot.AlignedData;
  series: SeriesSpec[];
  xLabel: string;
  yLabel: string;
  height?: number;
  logX?: boolean;
  logY?: boolean;
  /** Changing this re-runs setData without rebuilding the chart. */
  version: number;
  ariaLabel: string;
  /** Simulated time of the timeline cursor, drawn as a vertical line. */
  cursor?: number;
  /** Times of logged user actions, drawn as ticks along the top. */
  markers?: number[];
  /** Click or drag on the plot: seek to that x value. */
  onSeek?: (x: number) => void;
}

const FONT = '14px "VT323", monospace';

/** Thin wrapper around uPlot: canvas-based, cheap to update every 100 ms. */
export function Chart({
  data, series, xLabel, yLabel, height = 170, logX, logY, version, ariaLabel, cursor, markers, onSeek,
}: Props) {
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const dataRef = useRef(data);
  dataRef.current = data;
  const cursorRef = useRef(cursor);
  cursorRef.current = cursor;
  const markersRef = useRef(markers);
  markersRef.current = markers;
  const seekRef = useRef(onSeek);
  seekRef.current = onSeek;

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const ink = token('--ink');
    const axis = {
      stroke: ink,
      grid: { stroke: token('--rule'), width: 1, dash: [1, 3] },
      ticks: { stroke: ink, width: 1 },
      font: FONT,
      labelFont: FONT,
    };
    const opts: uPlot.Options = {
      width: el.clientWidth || 300,
      height,
      legend: { show: true, live: false },
      cursor: { drag: { x: false, y: false }, points: { size: 6 } },
      scales: {
        x: { time: false, distr: logX ? 3 : 1 },
        y: { distr: logY ? 3 : 1 },
      },
      axes: [
        { ...axis, values: (_u, splits) => splits.map((v) => (v == null ? '' : fmtAxis(v))), label: xLabel, labelSize: 18, size: 34 },
        { ...axis, values: (_u, splits) => splits.map((v) => (v == null ? '' : fmtAxis(v))), label: yLabel, labelSize: 18, size: 62 },
      ],
      series: [
        {},
        ...series.map((s) => ({
          label: s.label,
          stroke: s.color,
          width: s.width ?? 2,
          dash: s.dash,
          points: { show: s.points ?? false, size: 6, fill: s.color },
          spanGaps: true,
        })),
      ],
      hooks: {
        ready: [
          (u) => {
            if (!seekRef.current) return;
            const seekTo = (e: PointerEvent) => {
              const rect = u.over.getBoundingClientRect();
              seekRef.current?.(u.posToVal(e.clientX - rect.left, 'x'));
            };
            u.over.style.cursor = 'ew-resize';
            u.over.addEventListener('pointerdown', (e) => {
              u.over.setPointerCapture(e.pointerId);
              seekTo(e);
            });
            u.over.addEventListener('pointermove', (e) => {
              if (u.over.hasPointerCapture(e.pointerId)) seekTo(e);
            });
          },
        ],
        draw: [
          (u) => {
            const ctx = u.ctx;
            const { left, top, width, height: h } = u.bbox;
            const dpr = window.devicePixelRatio || 1;
            const [x0, x1] = [u.scales.x.min ?? 0, u.scales.x.max ?? 1];
            const px = (v: number) => left + ((v - x0) / (x1 - x0)) * width;
            ctx.save();
            ctx.beginPath();
            ctx.rect(left, top, width, h);
            ctx.clip();
            ctx.strokeStyle = ink;
            ctx.lineWidth = dpr;
            for (const t of markersRef.current ?? []) {
              if (t < x0 || t > x1) continue;
              ctx.beginPath();
              ctx.moveTo(Math.round(px(t)) + 0.5, top);
              ctx.lineTo(Math.round(px(t)) + 0.5, top + 8 * dpr);
              ctx.stroke();
            }
            const c = cursorRef.current;
            if (c !== undefined && c >= x0 && c <= x1) {
              ctx.strokeStyle = token('--red');
              ctx.lineWidth = 2 * dpr;
              ctx.beginPath();
              ctx.moveTo(Math.round(px(c)), top);
              ctx.lineTo(Math.round(px(c)), top + h);
              ctx.stroke();
            }
            ctx.restore();
          },
        ],
      },
    };
    plot.current = new uPlot(opts, dataRef.current, el);
    const ro = new ResizeObserver(() => {
      if (plot.current && el.clientWidth > 0) plot.current.setSize({ width: el.clientWidth, height });
    });
    ro.observe(el);
    return () => {
      ro.disconnect();
      plot.current?.destroy();
      plot.current = null;
    };
    // Rebuild only when the chart's shape changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [series.map((s) => s.label + s.color).join('|'), xLabel, yLabel, height, logX, logY, !!onSeek]);

  useEffect(() => {
    plot.current?.setData(dataRef.current, true);
  }, [version]);

  return <div className="chart" ref={host} role="img" aria-label={ariaLabel} />;
}
