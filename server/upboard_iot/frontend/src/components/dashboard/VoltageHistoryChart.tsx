import { useEffect, useMemo, useRef } from "react";
import { LineChart } from "echarts/charts";
import {
  DataZoomComponent,
  GridComponent,
  MarkAreaComponent,
  MarkLineComponent,
  TooltipComponent,
} from "echarts/components";
import { graphic, init, use, type EChartsCoreOption, type EChartsType } from "echarts/core";
import { CanvasRenderer } from "echarts/renderers";
import { TimeRangeSelector } from "@/components/dashboard/TimeRangeSelector";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { TelemetryHistoryPoint } from "@/types/telemetry";

interface VoltageHistoryChartProps {
  history: TelemetryHistoryPoint[];
}

const ranges = ["5 min", "30 min", "1 h", "6 h", "24 h"] as const;

use([
  LineChart,
  GridComponent,
  TooltipComponent,
  DataZoomComponent,
  MarkAreaComponent,
  MarkLineComponent,
  CanvasRenderer,
]);

export function VoltageHistoryChart({ history }: VoltageHistoryChartProps) {
  const chartRef = useRef<HTMLDivElement>(null);
  const voltageRange = useDashboardStore((state) => state.voltageRange);
  const setVoltageRange = useDashboardStore((state) => state.setVoltageRange);
  const values = useMemo(() => history.map((point) => point.voltageV), [history]);
  const current = values.at(-1) ?? null;
  const average = values.length > 0 ? values.reduce((sum, value) => sum + value, 0) / values.length : null;
  const minimum = values.length > 0 ? Math.min(...values) : null;
  const maximum = values.length > 0 ? Math.max(...values) : null;

  useEffect(() => {
    if (!chartRef.current) return;
    let chart: EChartsType | undefined;
    let cancelled = false;
    const resizeObserver = new ResizeObserver(() => chart?.resize());

    if (!cancelled && chartRef.current) {
      chart = init(chartRef.current, undefined, { renderer: "canvas" });
      const option: EChartsCoreOption = {
        animationDuration: 700,
        animationEasing: "cubicOut",
        grid: { top: 16, right: 14, bottom: 40, left: 38 },
        tooltip: {
          trigger: "axis",
          backgroundColor: "rgba(16,18,17,0.94)",
          borderWidth: 0,
          textStyle: { color: "#fff", fontSize: 11 },
        },
        xAxis: {
          type: "time",
          axisLine: { lineStyle: { color: "#e4e7e3" } },
          axisTick: { show: false },
          axisLabel: { color: "#9a9f9b", fontSize: 9, hideOverlap: true },
          splitLine: { show: false },
        },
        yAxis: {
          type: "value",
          min: 23.6,
          max: 25.1,
          interval: 0.5,
          axisLine: { show: false },
          axisTick: { show: false },
          axisLabel: { color: "#9a9f9b", fontSize: 9, formatter: "{value}V" },
          splitLine: { lineStyle: { color: "#edf0ec" } },
        },
        dataZoom: [
          { type: "inside", filterMode: "none", zoomOnMouseWheel: true, moveOnMouseMove: true },
          { type: "slider", height: 12, bottom: 4, borderColor: "transparent", backgroundColor: "#edf0ed", fillerColor: "rgba(40,226,154,0.25)", handleSize: 0, showDetail: false },
        ],
        series: [
          {
            name: "24V Bus",
            type: "line",
            smooth: 0.32,
            showSymbol: false,
            sampling: "lttb",
            lineStyle: { color: "#23c984", width: 2.5 },
            areaStyle: {
              color: new graphic.LinearGradient(0, 0, 0, 1, [
                { offset: 0, color: "rgba(40,226,154,0.20)" },
                { offset: 1, color: "rgba(40,226,154,0.01)" },
              ]),
            },
            data: history.map((point) => [point.timestamp, point.voltageV]),
            markArea: {
              silent: true,
              itemStyle: { color: "rgba(40,226,154,0.055)" },
              data: [[{ yAxis: 24.0 }, { yAxis: 24.9 }]],
            },
            markLine: {
              symbol: "none",
              silent: true,
              label: { show: false },
              lineStyle: { color: "#ff6268", type: "dashed", width: 1 },
              data: [{ yAxis: 23.8 }, { yAxis: 25.0 }],
            },
          },
        ],
      };
      chart.setOption(option);
      resizeObserver.observe(chartRef.current);
    }

    return () => {
      cancelled = true;
      resizeObserver.disconnect();
      chart?.dispose();
    };
  }, [history]);

  return (
    <article className="dashboard-card min-h-[438px] p-4.5">
      <div className="flex items-start justify-between gap-3">
        <div>
          <h2 className="m-0 text-[16px] font-semibold tracking-[-0.025em]">24V Bus History</h2>
          <p className="mb-0 mt-1 text-[10px] text-[var(--muted)]">Voltage stability and alarm thresholds</p>
        </div>
        <TimeRangeSelector options={ranges} value={voltageRange} onChange={setVoltageRange} compact />
      </div>
      <div className="mt-4 grid grid-cols-4 gap-2">
        {[
          ["Current", current],
          ["Average", average],
          ["Minimum", minimum],
          ["Maximum", maximum],
        ].map(([label, value]) => (
          <div key={String(label)} className="rounded-[12px] bg-[var(--surface-muted)] px-2.5 py-2">
            <div className="text-[9px] text-[var(--faint)]">{label}</div>
            <div className="mt-1 text-[11px] font-semibold">{typeof value === "number" ? `${value.toFixed(2)} V` : "Unavailable"}</div>
          </div>
        ))}
      </div>
      {history.length > 0 ? <div ref={chartRef} className="mt-2 h-[296px] w-full" role="img" aria-label="24 volt bus history chart" /> : <div className="mt-4 grid h-[296px] place-items-center rounded-[16px] bg-[var(--surface-muted)] text-[11px] text-[var(--muted)]">No 24V history is available.</div>}
    </article>
  );
}
