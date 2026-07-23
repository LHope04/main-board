import {
  Bell,
  CircleHelp,
  Cpu,
  FileText,
  LayoutDashboard,
  Map,
  Settings,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

const items = [LayoutDashboard, Cpu, Map, Bell, FileText, Settings];

export function IconSidebar() {
  return (
    <aside className="col-[1] row-[1/3] flex w-[68px] flex-col items-center rounded-[22px] bg-[var(--ink)] py-4 text-white shadow-[0_18px_40px_rgba(16,18,17,0.13)]">
      <div className="mb-6 grid size-10 place-items-center rounded-[13px] bg-[var(--accent)] text-[#07150f]">
        <span className="text-sm font-black tracking-[-0.08em]">TR</span>
      </div>
      <nav className="flex flex-col items-center gap-2" aria-label="Section navigation">
        {items.map((Icon, index) => (
          <Button
            key={index}
            aria-label={["Overview", "Devices", "Map", "Alerts", "Reports", "Settings"][index]}
            variant="darkGhost"
            size="icon"
            className={cn(
              "relative size-10 rounded-[12px]",
              index === 0 && "bg-white/10 text-white after:absolute after:-right-[14px] after:h-5 after:w-[3px] after:rounded-l-full after:bg-[var(--accent)]",
            )}
          >
            <Icon size={18} strokeWidth={1.8} />
          </Button>
        ))}
      </nav>
      <div className="mt-auto flex flex-col gap-2 border-t border-white/8 pt-3">
        <Button aria-label="Help" variant="darkGhost" size="icon" className="size-10 rounded-[12px]"><CircleHelp size={18} /></Button>
        <Button aria-label="Settings" variant="darkGhost" size="icon" className="size-10 rounded-[12px]"><Settings size={18} /></Button>
      </div>
    </aside>
  );
}
