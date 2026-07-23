import { lazy, Suspense } from "react";
import { motion, useReducedMotion } from "motion/react";
import {
  BatteryCharging,
  Clock3,
  Gauge,
  MapPinned,
  Radio,
  ServerCog,
  Signal,
  SmartphoneCharging,
  Zap,
} from "lucide-react";
import { ActuatorStatusPanel } from "@/components/dashboard/ActuatorStatusPanel";
import { DashboardHeader } from "@/components/dashboard/DashboardHeader";
import { OfflineBanner } from "@/components/dashboard/DashboardStates";
import { LiveIndicator } from "@/components/dashboard/LiveIndicator";
import { NtcSensorPanel } from "@/components/dashboard/NtcSensorPanel";
import { SummaryCard } from "@/components/dashboard/SummaryCard";
import { SystemHealthCard } from "@/components/dashboard/SystemHealthCard";
import { TelemetryMetricCard } from "@/components/dashboard/TelemetryMetricCard";
import { Skeleton } from "@/components/ui/skeleton";
import { formatClock, formatRelativeSeconds } from "@/lib/utils";
import type { CoolingDevice, DeviceTelemetryBundle } from "@/types/telemetry";

const GpsMapPanel = lazy(() => import("@/components/dashboard/GpsMapPanel").then((module) => ({ default: module.GpsMapPanel })));
const VoltageHistoryChart = lazy(() => import("@/components/dashboard/VoltageHistoryChart").then((module) => ({ default: module.VoltageHistoryChart })));

interface DashboardContentProps {
  device: CoolingDevice;
  bundle: DeviceTelemetryBundle;
  refreshing: boolean;
  onRefresh: () => void;
  devices: CoolingDevice[];
}

function formatMetric(value: number | null, digits: number, unit: string) {
  return value == null ? "Unavailable" : `${value.toFixed(digits)} ${unit}`;
}

export function DashboardContent({ device, bundle, refreshing, onRefresh, devices }: DashboardContentProps) {
  const reduceMotion = useReducedMotion();
  const { telemetry } = bundle;
  const capturedAt = telemetry.capturedAt ? new Date(telemetry.capturedAt) : null;
  const powerW = telemetry.busVoltageV != null && telemetry.busCurrentA != null
    ? telemetry.busVoltageV * telemetry.busCurrentA
    : null;
  const onlineDeviceCount = devices.filter((item) => item.status === "online").length;
  const availability = devices.length > 0 ? (onlineDeviceCount / devices.length) * 100 : 0;
  const gpsCoordinates = telemetry.gps.latitude != null && telemetry.gps.longitude != null
    ? `${telemetry.gps.latitude.toFixed(4)}, ${telemetry.gps.longitude.toFixed(4)}`
    : "Unavailable";

  return (
    <motion.div
      key={device.id}
      initial={reduceMotion ? false : { opacity: 0, y: 8 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.36, ease: [0.16, 1, 0.3, 1] }}
      className="space-y-3.5 pb-4"
    >
      <DashboardHeader lastUpdated={capturedAt} refreshing={refreshing} onRefresh={onRefresh} />

      {device.status === "offline" ? <OfflineBanner lastReport={formatRelativeSeconds(device.lastReportSeconds)} /> : null}

      <section className="grid grid-cols-2 gap-3 xl:grid-cols-4" aria-label="Fleet summary">
        <SummaryCard label="Total Devices" value={String(devices.length)} description="Registered cooling units" icon={ServerCog} />
        <SummaryCard label="Online Devices" value={String(onlineDeviceCount)} description={`${availability.toFixed(1)}% availability`} icon={Radio} progress={availability} />
        <SummaryCard label="Selected Device" value={device.sn} description={device.name} icon={Gauge} dark />
        <SummaryCard label="Last Report" value={capturedAt ? formatClock(capturedAt) : "Unavailable"} description={capturedAt ? formatRelativeSeconds(device.lastReportSeconds) : "No telemetry received"} icon={Clock3} trailing={device.status === "online" ? <LiveIndicator /> : undefined} />
      </section>

      <section className="grid grid-cols-1 gap-3 xl:grid-cols-12" aria-label="Realtime system telemetry">
        <div className="xl:col-span-5"><SystemHealthCard telemetry={telemetry} deviceStatus={device.status} /></div>
        <div className="grid grid-cols-3 gap-3 xl:col-span-7">
          <TelemetryMetricCard title="RSSI" value={formatMetric(telemetry.rssiDbm, 0, "dBm")} status={telemetry.rssiDbm == null ? "Unavailable" : telemetry.rssiDbm >= -85 ? "Good" : "Weak"} secondary="Cellular signal" icon={Signal} sparkline={bundle.rssiHistory} accent="purple" />
          <TelemetryMetricCard
            title="Last Valid GPS"
            value={gpsCoordinates}
            status={telemetry.gps.valid ? (telemetry.gps.accuracyM == null ? "Accuracy unavailable" : `Accuracy ±${telemetry.gps.accuracyM}m`) : "GPS Signal Lost"}
            secondary={`Updated ${formatRelativeSeconds(telemetry.gps.updatedSeconds)}`}
            icon={MapPinned}
            className="col-span-2"
            accent={telemetry.gps.valid ? "green" : "orange"}
          />
          <TelemetryMetricCard title="24V Bus Voltage" value={formatMetric(telemetry.busVoltageV, 2, "V")} status={telemetry.busVoltageV == null ? "Unavailable" : telemetry.busVoltageV >= 24 && telemetry.busVoltageV <= 24.9 ? "Stable" : "Check range"} secondary="Normal range 24.0-24.9 V" icon={Zap} sparkline={bundle.voltageSparkline} />
          <TelemetryMetricCard title="24V Bus Current" value={formatMetric(telemetry.busCurrentA, 2, "A")} status={powerW == null ? "Unavailable" : `${powerW.toFixed(1)} W`} secondary="Calculated load" icon={BatteryCharging} />
          <TelemetryMetricCard title="Vehicle Input" value={formatMetric(telemetry.vehicleInputV, 2, "V")} status={telemetry.vehicleCharging == null ? "Unavailable" : telemetry.vehicleCharging ? "Charging" : "Not charging"} secondary="12V vehicle supply" icon={SmartphoneCharging} accent="orange" />
        </div>
      </section>

      <section className="grid grid-cols-1 gap-3 xl:grid-cols-12" aria-label="Location and voltage history">
        <div className="xl:col-span-7">
          <Suspense fallback={<Skeleton className="h-[438px] rounded-[22px]" />}>
            <GpsMapPanel gps={telemetry.gps} track={bundle.gpsTrack} routeStats={bundle.routeStats} deviceKey={device.id} />
          </Suspense>
        </div>
        <div className="xl:col-span-5">
          <Suspense fallback={<Skeleton className="h-[438px] rounded-[22px]" />}>
            <VoltageHistoryChart history={bundle.voltageHistory} />
          </Suspense>
        </div>
      </section>

      <ActuatorStatusPanel outputs={telemetry.outputs} />
      <NtcSensorPanel channels={telemetry.ntcChannels} />
    </motion.div>
  );
}
