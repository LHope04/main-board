import * as React from "react";
import { Slot } from "@radix-ui/react-slot";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const buttonVariants = cva(
  "inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap rounded-[11px] text-sm font-medium outline-none transition-[transform,background-color,border-color,color,box-shadow] duration-200 focus-visible:ring-2 focus-visible:ring-[var(--accent)]/35 disabled:pointer-events-none disabled:opacity-50 active:translate-y-px",
  {
    variants: {
      variant: {
        default: "bg-[var(--ink)] text-white hover:bg-[#242825]",
        accent: "bg-[var(--accent)] text-[#07140f] hover:bg-[#21cf8c]",
        outline: "border border-[var(--border)] bg-white text-[var(--text)] hover:bg-[var(--surface-muted)]",
        ghost: "text-[var(--muted)] hover:bg-black/5 hover:text-[var(--text)]",
        darkGhost: "text-white/65 hover:bg-white/8 hover:text-white",
      },
      size: {
        default: "h-10 px-4",
        sm: "h-8 px-3 text-xs",
        icon: "size-10 p-0",
        iconSm: "size-8 p-0",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  },
);

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  asChild?: boolean;
}

export function Button({ className, variant, size, asChild = false, ...props }: ButtonProps) {
  const Comp = asChild ? Slot : "button";
  return <Comp className={cn(buttonVariants({ variant, size, className }))} {...props} />;
}
