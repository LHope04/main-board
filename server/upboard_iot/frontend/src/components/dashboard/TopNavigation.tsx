import {
  Bell,
  ChevronDown,
  Command,
  LogOut,
  RefreshCw,
  Search,
  Wifi,
  WifiOff,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { cn } from "@/lib/utils";
import { useDashboardStore } from "@/stores/dashboard-store";

const navItems = ["Overview", "Telemetry", "Map & Trips", "Alerts", "Devices", "Settings"];

interface TopNavigationProps {
  username: string;
  onLogout: () => void;
  logoutPending: boolean;
}

export function TopNavigation({ username, onLogout, logoutPending }: TopNavigationProps) {
  const connectionStatus = useDashboardStore((state) => state.connectionStatus);
  const autoRefresh = useDashboardStore((state) => state.autoRefresh);
  const toggleAutoRefresh = useDashboardStore((state) => state.toggleAutoRefresh);
  const reconnecting = connectionStatus === "reconnecting";
  const offline = connectionStatus === "offline";
  const initials = username.split(/\s+/).filter(Boolean).slice(0, 2).map((part) => part[0]?.toUpperCase()).join("") || "AD";

  return (
    <header className="col-[2/4] row-[1] flex h-[68px] min-w-0 items-center rounded-[20px] bg-[var(--ink)] px-5 text-white shadow-[0_16px_34px_rgba(16,18,17,0.12)]">
      <div className="flex min-w-[225px] items-center gap-3">
        <div className="grid size-9 place-items-center rounded-[11px] bg-[var(--accent)] text-[#07150f] shadow-[inset_0_1px_0_rgba(255,255,255,0.4)]">
          <Command size={18} strokeWidth={2.2} />
        </div>
        <div className="leading-tight">
          <div className="text-[14px] font-bold tracking-[0.12em]">THERMORIDE</div>
          <div className="mt-1 text-[10px] text-white/45">Motorcycle Cooling Intelligence</div>
        </div>
      </div>

      <nav className="ml-5 hidden min-w-0 items-center gap-1.5 xl:flex" aria-label="Primary navigation">
        {navItems.map((item) => (
          <Button
            key={item}
            variant="darkGhost"
            size="sm"
            className={cn(
              "relative h-9 px-3 text-xs",
              item === "Overview" &&
                "bg-white/[0.09] text-white after:absolute after:-bottom-[14px] after:left-1/2 after:h-[2px] after:w-7 after:-translate-x-1/2 after:rounded-full after:bg-[var(--accent)] after:shadow-[0_0_12px_rgba(40,226,154,0.8)]",
            )}
          >
            {item}
          </Button>
        ))}
      </nav>

      <div className="ml-auto flex items-center gap-2.5">
        <label className="relative hidden 2xl:block">
          <span className="sr-only">Global search</span>
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 text-white/40" size={15} />
          <Input
            placeholder="Search devices or commands"
            className="h-9 w-[218px] border-white/10 bg-white/[0.07] pl-9 pr-12 text-xs text-white placeholder:text-white/35 focus:border-white/20 focus:ring-2 focus:ring-[var(--accent)]/20"
          />
          <span className="absolute right-2.5 top-1/2 -translate-y-1/2 rounded-[5px] border border-white/10 bg-white/5 px-1.5 py-0.5 text-[9px] text-white/35">⌘ K</span>
        </label>

        <div className={cn("hidden h-9 items-center gap-2 rounded-[11px] border px-3 text-[11px] font-medium lg:flex", reconnecting ? "border-orange-400/25 bg-orange-400/10 text-orange-200" : offline ? "border-white/10 bg-white/5 text-white/45" : "border-emerald-400/20 bg-emerald-400/10 text-emerald-200")}>
          {reconnecting ? <RefreshCw size={13} className="animate-spin" /> : offline ? <WifiOff size={13} /> : <Wifi size={13} />}
          {reconnecting ? "Reconnecting" : offline ? "Offline" : "SSE Live"}
        </div>

        <Button
          type="button"
          size="sm"
          variant="darkGhost"
          onClick={toggleAutoRefresh}
          className="hidden h-9 border border-white/8 bg-white/[0.045] px-3 text-[11px] lg:inline-flex"
        >
          {autoRefresh ? <RefreshCw size={13} /> : <WifiOff size={13} />}
          Auto Refresh {autoRefresh ? "On" : "Off"}
        </Button>

        <Button aria-label="Notifications" size="iconSm" variant="darkGhost" className="relative size-9 border border-white/8 bg-white/[0.045]">
          <Bell size={16} />
          <span className="absolute right-2 top-2 size-1.5 rounded-full bg-[var(--fault)]" />
        </Button>

        <div className="group relative">
          <button className="flex items-center gap-2 rounded-[11px] p-1 pr-1.5 text-left transition hover:bg-white/[0.06]" type="button" aria-label={`Account menu for ${username}`}>
            <span className="grid size-8 place-items-center rounded-[9px] bg-gradient-to-br from-[#d7e0dc] to-[#8da198] text-[11px] font-bold text-[#172019]">{initials}</span>
            <ChevronDown size={13} className="text-white/35" />
          </button>
          <div className="invisible absolute right-0 top-[calc(100%+8px)] z-[600] w-44 translate-y-1 rounded-[13px] border border-black/5 bg-white p-1.5 opacity-0 shadow-[0_18px_44px_rgba(16,18,17,0.18)] transition group-focus-within:visible group-focus-within:translate-y-0 group-focus-within:opacity-100 group-hover:visible group-hover:translate-y-0 group-hover:opacity-100">
            <div className="truncate px-2.5 py-2 text-[10px] font-semibold text-[var(--text)]">{username}</div>
            <Button type="button" variant="ghost" size="sm" onClick={onLogout} disabled={logoutPending} className="h-8 w-full justify-start text-[11px] text-red-600 hover:bg-red-50 hover:text-red-700"><LogOut size={13} /> {logoutPending ? "Signing out..." : "Sign out"}</Button>
          </div>
        </div>
      </div>
    </header>
  );
}
