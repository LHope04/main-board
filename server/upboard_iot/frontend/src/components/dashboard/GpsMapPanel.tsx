import { useEffect, useMemo, useRef, useState } from "react";
import L from "leaflet";
import {
  CirclePlay,
  Expand,
  Focus,
  Layers3,
  LocateFixed,
  Pause,
} from "lucide-react";
import { MapContainer, Marker, Polyline, TileLayer, useMap } from "react-leaflet";
import { LiveIndicator } from "@/components/dashboard/LiveIndicator";
import { TimeRangeSelector } from "@/components/dashboard/TimeRangeSelector";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { GpsData, GpsTrackPoint } from "@/types/telemetry";

interface GpsMapPanelProps {
  gps: GpsData;
  track: GpsTrackPoint[];
  deviceKey: string;
  routeStats: {
    distanceKm: number;
    durationMinutes: number;
    averageSpeedKmh: number;
  } | null;
}

const mapRanges = ["Live", "1H", "6H", "24H", "Custom"] as const;

function MapViewport({ points, focusToken }: { points: Array<[number, number]>; focusToken: number }) {
  const map = useMap();
  useEffect(() => {
    if (points.length === 0) return;
    if (points.length === 1) {
      const point = points[0];
      if (point) map.setView(point, 15, { animate: true });
      return;
    }
    map.fitBounds(L.latLngBounds(points), { padding: [36, 36], maxZoom: 15, animate: true });
  }, [focusToken, map, points]);
  return null;
}

const currentIcon = L.divIcon({ className: "", html: '<div class="gps-breathing-marker"></div>', iconSize: [18, 18], iconAnchor: [9, 9] });
const startIcon = L.divIcon({ className: "", html: '<div class="gps-start-marker"></div>', iconSize: [18, 18], iconAnchor: [9, 9] });
const endIcon = L.divIcon({ className: "", html: '<div class="gps-end-marker"></div>', iconSize: [18, 18], iconAnchor: [9, 9] });

export function GpsMapPanel({ gps, track, deviceKey, routeStats }: GpsMapPanelProps) {
  const mapRange = useDashboardStore((state) => state.mapRange);
  const setMapRange = useDashboardStore((state) => state.setMapRange);
  const [satelliteLike, setSatelliteLike] = useState(false);
  const [replaying, setReplaying] = useState(false);
  const [replayIndex, setReplayIndex] = useState(track.length - 1);
  const [focusToken, setFocusToken] = useState(0);
  const panelRef = useRef<HTMLElement>(null);

  useEffect(() => {
    setReplayIndex(track.length - 1);
    setReplaying(false);
  }, [deviceKey, track.length]);

  useEffect(() => {
    if (!replaying || track.length < 2) return;
    const timer = window.setInterval(() => {
      setReplayIndex((current) => {
        if (current >= track.length - 1) return 0;
        return current + 1;
      });
    }, 650);
    return () => window.clearInterval(timer);
  }, [replaying, track.length]);

  const points = useMemo<Array<[number, number]>>(
    () => track.map((point) => [point.latitude, point.longitude]),
    [track],
  );
  const validPoints = useMemo<Array<[number, number]>>(
    () => track.filter((point) => point.valid).map((point) => [point.latitude, point.longitude]),
    [track],
  );
  const invalidPoints = useMemo<Array<[number, number]>>(() => {
    const invalidIndexes = track.map((point, index) => (!point.valid ? index : -1)).filter((index) => index >= 0);
    if (invalidIndexes.length === 0) return [];
    const first = Math.max(0, (invalidIndexes[0] ?? 0) - 1);
    const last = Math.min(track.length - 1, (invalidIndexes.at(-1) ?? 0) + 1);
    return track.slice(first, last + 1).map((point) => [point.latitude, point.longitude]);
  }, [track]);

  const replayPoint = track[Math.max(0, replayIndex)] ?? track.at(-1);
  const startPoint = track[0];
  const endPoint = track.at(-1);

  const toggleFullscreen = async () => {
    if (!panelRef.current) return;
    if (document.fullscreenElement) await document.exitFullscreen();
    else await panelRef.current.requestFullscreen();
  };

  return (
    <article ref={panelRef} className="dashboard-card relative min-h-[438px] overflow-hidden p-4.5 [container-type:inline-size]">
      <div className="flex items-start justify-between gap-4">
        <div>
          <div className="flex items-center gap-2">
            <h2 className="m-0 text-[16px] font-semibold tracking-[-0.025em]">Vehicle Location</h2>
            {gps.valid ? <LiveIndicator /> : null}
          </div>
          <p className="mb-0 mt-1 text-[10px] text-[var(--muted)]">Live GPS and recent route</p>
        </div>
        <TimeRangeSelector options={mapRanges} value={mapRange} onChange={setMapRange} compact />
      </div>

      <div className="relative mt-3 h-[318px] overflow-hidden rounded-[17px] border border-[var(--border)] bg-[#e7ebe7]">
        <MapContainer key={deviceKey} center={points.at(-1) ?? [29.563, 106.5516]} zoom={points.length > 0 ? 14 : 5} zoomControl={false} attributionControl>
          <TileLayer
            key={satelliteLike ? "dark" : "light"}
            attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> &copy; CARTO'
            url={satelliteLike
              ? "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png"
              : "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png"}
          />
          <MapViewport points={points} focusToken={focusToken} />
          {validPoints.length > 1 ? <Polyline positions={validPoints} pathOptions={{ color: "#1bbd7a", weight: 4, opacity: 0.82, lineCap: "round" }} /> : null}
          {invalidPoints.length > 1 ? <Polyline positions={invalidPoints} pathOptions={{ color: "#818783", weight: 3, opacity: 0.7, dashArray: "6 8" }} /> : null}
          {startPoint ? <Marker position={[startPoint.latitude, startPoint.longitude]} icon={startIcon} /> : null}
          {endPoint ? <Marker position={[endPoint.latitude, endPoint.longitude]} icon={endIcon} /> : null}
          {replayPoint ? <Marker position={[replayPoint.latitude, replayPoint.longitude]} icon={currentIcon} /> : null}
        </MapContainer>

        <div className="absolute right-3 top-3 z-[500] flex flex-col gap-2">
          <Button aria-label="Center map on device" size="iconSm" variant="outline" className="size-8 bg-white/90 shadow-sm" onClick={() => setFocusToken((value) => value + 1)}><Focus size={14} /></Button>
          <Button aria-label="Toggle map layer" size="iconSm" variant="outline" className="size-8 bg-white/90 shadow-sm" onClick={() => setSatelliteLike((value) => !value)}><Layers3 size={14} /></Button>
          <Button aria-label="Fullscreen map" size="iconSm" variant="outline" className="size-8 bg-white/90 shadow-sm" onClick={() => void toggleFullscreen()}><Expand size={14} /></Button>
        </div>

        <Button
          type="button"
          variant="outline"
          size="sm"
          onClick={() => setReplaying((value) => !value)}
          className="absolute bottom-3 left-3 z-[500] h-8 border-white/70 bg-white/90 px-3 text-[10px] shadow-sm"
        >
          {replaying ? <Pause size={13} /> : <CirclePlay size={13} />}
          {replaying ? "Pause route" : "Replay route"}
        </Button>

        {!gps.valid ? (
          <div className="absolute inset-x-3 top-3 z-[500] mx-auto max-w-[270px] rounded-[14px] border border-orange-200 bg-white/92 px-3.5 py-2.5 shadow-[0_10px_26px_rgba(58,47,34,0.1)] backdrop-blur-sm">
            <div className="flex items-center gap-2 text-[11px] font-semibold text-orange-700"><LocateFixed size={14} /> GPS Signal Lost</div>
            <div className="mt-1 text-[10px] text-[var(--muted)]">{points.length > 0 ? "Showing last valid position" : "No valid position has been received"}</div>
          </div>
        ) : null}
      </div>

      <div className="mt-3 grid grid-cols-4 gap-2.5">
        {[
          ["Distance", routeStats ? `${routeStats.distanceKm.toFixed(1)} km` : "Unavailable"],
          ["Duration", routeStats ? `${Math.round(routeStats.durationMinutes)} min` : "Unavailable"],
          ["Average Speed", routeStats ? `${routeStats.averageSpeedKmh.toFixed(1)} km/h` : "Unavailable"],
          ["GPS Accuracy", gps.accuracyM == null ? "Unavailable" : `±${gps.accuracyM} m`],
        ].map(([label, value]) => (
          <div key={label} className="rounded-[12px] bg-[var(--surface-muted)] px-3 py-2">
            <div className="text-[9px] text-[var(--faint)]">{label}</div>
            <div className={cn("mt-1 text-[11px] font-semibold", label === "GPS Accuracy" && !gps.valid ? "text-orange-600" : "text-[var(--text)]")}>{value}</div>
          </div>
        ))}
      </div>
    </article>
  );
}
