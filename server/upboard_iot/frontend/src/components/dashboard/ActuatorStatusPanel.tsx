import { Activity } from "lucide-react";
import { ActuatorStatusItem } from "@/components/dashboard/ActuatorStatusItem";
import type { ActuatorOutput } from "@/types/telemetry";

interface ActuatorStatusPanelProps {
  outputs: ActuatorOutput[];
}

export function ActuatorStatusPanel({ outputs }: ActuatorStatusPanelProps) {
  return (
    <article className="dashboard-card p-4.5">
      <div className="flex items-start justify-between gap-3">
        <div>
          <h2 className="m-0 text-[16px] font-semibold tracking-[-0.025em]">Actuator Status</h2>
          <p className="mb-0 mt-1 text-[10px] text-[var(--muted)]">Read-only operating states from the selected unit</p>
        </div>
        <span className="grid size-8 place-items-center rounded-[10px] bg-emerald-50 text-emerald-700"><Activity size={15} /></span>
      </div>
      <div className="mt-4 grid grid-cols-2 gap-2.5 xl:grid-cols-5">
        {outputs.map((output) => <ActuatorStatusItem key={output.id} output={output} />)}
      </div>
    </article>
  );
}
