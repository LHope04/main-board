import { AlertTriangle, PackageOpen, RadioTower, RefreshCw } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";

export function DashboardLoadingState() {
  return (
    <div className="space-y-4" aria-label="Loading dashboard">
      <div className="flex items-end justify-between"><div><Skeleton className="h-8 w-72" /><Skeleton className="mt-2 h-3 w-80" /></div><Skeleton className="h-9 w-52" /></div>
      <div className="grid grid-cols-4 gap-3">{Array.from({ length: 4 }, (_, index) => <Skeleton key={index} className="h-[126px] rounded-[22px]" />)}</div>
      <div className="grid grid-cols-12 gap-3"><Skeleton className="col-span-5 h-[305px] rounded-[22px]" /><div className="col-span-7 grid grid-cols-3 gap-3">{Array.from({ length: 5 }, (_, index) => <Skeleton key={index} className="h-[146px] rounded-[22px]" />)}</div></div>
      <div className="grid grid-cols-12 gap-3"><Skeleton className="col-span-7 h-[438px] rounded-[22px]" /><Skeleton className="col-span-5 h-[438px] rounded-[22px]" /></div>
    </div>
  );
}

interface StatePanelProps {
  kind: "empty" | "error";
  onRetry?: () => void;
}

export function DashboardStatePanel({ kind, onRetry }: StatePanelProps) {
  const error = kind === "error";
  const Icon = error ? AlertTriangle : PackageOpen;
  return (
    <div className="dashboard-card grid min-h-[calc(100dvh-112px)] place-items-center p-10 text-center">
      <div className="max-w-[350px]">
        <span className={`mx-auto grid size-14 place-items-center rounded-[18px] ${error ? "bg-red-50 text-red-600" : "bg-emerald-50 text-emerald-700"}`}><Icon size={24} /></span>
        <h1 className="mb-0 mt-5 text-[22px] font-semibold tracking-[-0.035em]">{error ? "Telemetry unavailable" : "No devices yet"}</h1>
        <p className="mb-0 mt-2 text-sm leading-6 text-[var(--muted)]">{error ? "The data service could not load this dashboard. Check the connection and try again." : "Registered cooling units will appear here after their first telemetry message is received."}</p>
        {error && onRetry ? <Button type="button" className="mt-5" onClick={onRetry}><RefreshCw size={15} /> Retry</Button> : null}
      </div>
    </div>
  );
}

export function OfflineBanner({ lastReport }: { lastReport: string }) {
  return (
    <div className="flex items-center gap-3 rounded-[16px] border border-orange-200 bg-orange-50 px-4 py-3 text-orange-900">
      <span className="grid size-9 place-items-center rounded-[11px] bg-white/70"><RadioTower size={17} /></span>
      <div><div className="text-xs font-semibold">Device offline</div><div className="mt-0.5 text-[10px] text-orange-800/70">Showing last known telemetry. Last report {lastReport}.</div></div>
    </div>
  );
}
