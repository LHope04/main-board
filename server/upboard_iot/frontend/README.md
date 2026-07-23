# THERMORIDE Telemetry Frontend

Premium motorcycle cooling telemetry dashboard built with React, TypeScript, Vite, Tailwind CSS, shadcn/ui-style owned components, Apache ECharts, Leaflet, Lucide, Zustand, and React Query.

## Run

```bash
npm install
npm run dev
```

The development server proxies `/api` and `/healthz` to `http://127.0.0.1:8000`. Override this with `VITE_DEV_API_TARGET` when FastAPI runs elsewhere.

The default data source is the authenticated FastAPI API. To run the self-contained design demo instead:

```bash
VITE_DATA_SOURCE=mock npm run dev
```

Production validation:

```bash
npm run typecheck
npm run build
```

## Mock state previews

Mock mode uses centralized data from `src/mocks` through the adapter in `src/services/telemetry.ts`.

- `/?state=empty`
- `/?state=error`
- `/?state=reconnecting`

Select `TRC-240718-011` to verify the offline state and `TRC-240720-018` to verify the GPS-invalid state.

Production uses Cookie-authenticated REST for the initial snapshot and SSE `/api/stream` to invalidate matching React Query data.
