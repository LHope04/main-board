import * as React from "react";
import { cn } from "@/lib/utils";

export function Input({ className, type = "text", ...props }: React.ComponentProps<"input">) {
  return (
    <input
      type={type}
      className={cn(
        "h-10 w-full rounded-[11px] border border-[var(--border)] bg-white px-3 text-sm text-[var(--text)] outline-none transition placeholder:text-[#9b9f9c] focus:border-[#a4b4aa] focus:ring-4 focus:ring-emerald-500/8 disabled:cursor-not-allowed disabled:opacity-50",
        className,
      )}
      {...props}
    />
  );
}
