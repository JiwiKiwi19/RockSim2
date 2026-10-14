import { useEffect, useRef } from 'react';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { fmtAxis } from './format';

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
}

const AXIS = { stroke: '#5B6673', grid: { stroke: '#E3E9EF', width: 1 }, ticks: { stroke: '#C9D4DE', width: 1 } };

/** Thin wrapper around uPlot: canvas-based, cheap to update every 100 ms. */
export function Chart({ data, series, xLabel, yLabel, height = 170, logX, logY, version, ariaLabel }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const dataRef = useRef(data);
  dataRef.current = data;

  useEffect(() => {
    const el = host.current;
    if (!el) return;
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
        { ...AXIS, values: (_u, splits) => splits.map((v) => (v == null ? '' : fmtAxis(v))), label: xLabel, labelSize: 18, size: 36, font: '11px "Instrument Sans", system-ui', labelFont: '500 11px "Instrument Sans", system-ui' },
        { ...AXIS, values: (_u, splits) => splits.map((v) => (v == null ? '' : fmtAxis(v))), label: yLabel, labelSize: 18, size: 58, font: '11px "Instrument Sans", system-ui', labelFont: '500 11px "Instrument Sans", system-ui' },
      ],
      series: [
        {},
        ...series.map((s) => ({
          label: s.label,
          stroke: s.color,
          width: s.width ?? 1.75,
          dash: s.dash,
          points: { show: s.points ?? false, size: 6, fill: s.color },
          spanGaps: false,
        })),
      ],
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
  }, [series.map((s) => s.label + s.color).join('|'), xLabel, yLabel, height, logX, logY]);

  useEffect(() => {
    plot.current?.setData(dataRef.current, true);
  }, [version]);

  return <div className="chart" ref={host} role="img" aria-label={ariaLabel} />;
}
