import type { HTMLAttributes } from "react";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const badgeVariants = cva(
  "inline-flex items-center gap-1.5 rounded-full border px-2.5 py-1 text-[11px] font-semibold leading-none",
  {
    variants: {
      variant: {
        neutral: "border-black/6 bg-black/[0.035] text-[var(--muted)]",
        online: "border-emerald-200 bg-emerald-50 text-emerald-700",
        delayed: "border-orange-200 bg-orange-50 text-orange-700",
        offline: "border-zinc-200 bg-zinc-100 text-zinc-500",
        fault: "border-red-200 bg-red-50 text-red-600",
        dark: "border-white/10 bg-white/7 text-white/75",
      },
    },
    defaultVariants: { variant: "neutral" },
  },
);

export interface BadgeProps
  extends HTMLAttributes<HTMLSpanElement>,
    VariantProps<typeof badgeVariants> {}

export function Badge({ className, variant, ...props }: BadgeProps) {
  return <span className={cn(badgeVariants({ variant }), className)} {...props} />;
}
