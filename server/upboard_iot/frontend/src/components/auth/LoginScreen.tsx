import { useState, type FormEvent } from "react";
import { Command, LockKeyhole, Radio, ShieldCheck, UserRound } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";

interface LoginScreenProps {
  loading: boolean;
  error: string | null;
  serviceUnavailable?: boolean;
  onLogin: (username: string, password: string) => void;
  onRetry?: () => void;
}

export function AuthLoadingScreen() {
  return (
    <main className="grid min-h-[100dvh] min-w-[1024px] place-items-center bg-[var(--page)] p-8" aria-label="Checking session">
      <div className="flex items-center gap-3 rounded-[18px] border border-white/80 bg-white px-5 py-4 shadow-[0_18px_50px_rgba(26,38,31,0.09)]">
        <span className="grid size-9 place-items-center rounded-[11px] bg-[var(--accent)] text-[#07150f]"><Command size={18} /></span>
        <div><div className="text-[12px] font-semibold tracking-[0.08em]">THERMORIDE</div><div className="mt-1 text-[10px] text-[var(--muted)]">Checking secure session...</div></div>
      </div>
    </main>
  );
}

export function LoginScreen({ loading, error, serviceUnavailable = false, onLogin, onRetry }: LoginScreenProps) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");

  const submit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!username.trim() || !password) return;
    onLogin(username.trim(), password);
  };

  return (
    <main className="grid min-h-[100dvh] min-w-[1024px] place-items-center bg-[var(--page)] p-8">
      <section className="grid w-full max-w-[1040px] grid-cols-[1.08fr_0.92fr] overflow-hidden rounded-[28px] border border-white/80 bg-white shadow-[0_32px_90px_rgba(26,38,31,0.12)]">
        <div className="relative overflow-hidden bg-[var(--ink)] p-11 text-white">
          <div className="absolute inset-0 bg-[radial-gradient(circle_at_80%_20%,rgba(40,226,154,0.22),transparent_28%),radial-gradient(circle_at_15%_90%,rgba(32,112,81,0.28),transparent_34%)]" />
          <div className="relative flex h-full min-h-[500px] flex-col">
            <div className="flex items-center gap-3">
              <span className="grid size-10 place-items-center rounded-[12px] bg-[var(--accent)] text-[#07150f]"><Command size={20} strokeWidth={2.2} /></span>
              <div>
                <div className="text-[15px] font-bold tracking-[0.12em]">THERMORIDE</div>
                <div className="mt-1 text-[10px] text-white/45">Motorcycle Cooling Intelligence</div>
              </div>
            </div>
            <div className="my-auto max-w-[430px]">
              <h1 className="m-0 text-[38px] font-semibold leading-[1.05] tracking-[-0.055em]">Live cooling telemetry, secured at the source.</h1>
              <p className="mb-0 mt-5 max-w-[38ch] text-[14px] leading-6 text-white/52">Sign in to inspect registered units, power history, GPS routes and actuator state.</p>
            </div>
            <div className="grid grid-cols-3 gap-2.5">
              {[
                [Radio, "SSE", "Live updates"],
                [ShieldCheck, "Cookie", "HttpOnly session"],
                [LockKeyhole, "Read only", "Telemetry access"],
              ].map(([Icon, value, label]) => {
                const FeatureIcon = Icon as typeof Radio;
                return <div key={String(value)} className="rounded-[15px] border border-white/8 bg-white/[0.055] p-3.5"><FeatureIcon size={15} className="text-[var(--accent)]" /><div className="mt-4 text-[12px] font-semibold">{String(value)}</div><div className="mt-1 text-[9px] text-white/38">{String(label)}</div></div>;
              })}
            </div>
          </div>
        </div>

        <div className="flex min-h-[588px] items-center p-12">
          <div className="w-full">
            <div className="grid size-11 place-items-center rounded-[14px] bg-emerald-50 text-emerald-700"><UserRound size={19} /></div>
            <h2 className="mb-0 mt-6 text-[27px] font-semibold tracking-[-0.045em]">Administrator sign in</h2>
            <p className="mb-0 mt-2 text-[12px] leading-5 text-[var(--muted)]">Use the credentials configured on the Upboard IoT server.</p>

            <form className="mt-8 space-y-5" onSubmit={submit}>
              <label className="block">
                <span className="mb-2 block text-[11px] font-semibold text-[var(--text)]">Username</span>
                <Input value={username} onChange={(event) => setUsername(event.target.value)} autoComplete="username" className="h-11 bg-[var(--surface-muted)] px-3.5 text-sm" disabled={loading || serviceUnavailable} />
              </label>
              <label className="block">
                <span className="mb-2 block text-[11px] font-semibold text-[var(--text)]">Password</span>
                <Input type="password" value={password} onChange={(event) => setPassword(event.target.value)} autoComplete="current-password" className="h-11 bg-[var(--surface-muted)] px-3.5 text-sm" disabled={loading || serviceUnavailable} />
              </label>
              {error ? <div role="alert" className="rounded-[13px] border border-red-100 bg-red-50 px-3.5 py-3 text-[11px] leading-5 text-red-700">{error}</div> : null}
              {serviceUnavailable && onRetry ? (
                <Button type="button" className="h-11 w-full" onClick={onRetry}>Retry connection</Button>
              ) : (
                <Button type="submit" className="h-11 w-full" disabled={loading || !username.trim() || !password}>{loading ? "Signing in..." : "Sign in"}</Button>
              )}
            </form>
          </div>
        </div>
      </section>
    </main>
  );
}
