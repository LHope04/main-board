import type {
  ActuatorOutput,
  CoolingDevice,
  DeviceTelemetryBundle,
  GpsTrackPoint,
  NtcChannel,
  TelemetryHistoryPoint,
} from "@/types/telemetry";

const ntcNames = [
  "Coolant Outlet",
  "Coolant Return",
  "Compressor Shell",
  "Evaporator",
  "Condenser",
  "Battery",
  "Power Board",
  "Ambient",
] as const;

const defaultOutputs: ActuatorOutput[] = [
  { id: "boost", name: "BOOST CONVERTER", status: "active", description: "24V boost stage enabled" },
  { id: "load", name: "LOAD OUTPUT", status: "active", description: "Main load rail energized" },
  { id: "fan", name: "CONDENSER FAN", status: "active", description: "Fan feedback within range" },
  { id: "pump", name: "COOLANT PUMP", status: "active", description: "Coolant circulation detected" },
  { id: "compressor", name: "COMPRESSOR", status: "active", description: "Compressor running normally" },
];

function seededWave(seed: number, count: number, center: number, amplitude: number) {
  return Array.from({ length: count }, (_, index) => {
    const primary = Math.sin((index + seed) / 4.6) * amplitude;
    const secondary = Math.cos((index + seed * 0.7) / 9.1) * amplitude * 0.38;
    return Number((center + primary + secondary).toFixed(3));
  });
}

function createVoltageHistory(seed: number): TelemetryHistoryPoint[] {
  const now = Date.now();
  const voltages = seededWave(seed, 72, 24.46 + seed * 0.01, 0.24);
  return voltages.map((voltageV, index) => ({
    timestamp: new Date(now - (voltages.length - index - 1) * 5_000).toISOString(),
    voltageV,
    currentA: Number((3.08 + Math.sin((index + seed) / 6) * 0.31).toFixed(3)),
  }));
}

function createGpsTrack(seed: number, gpsValid: boolean): GpsTrackPoint[] {
  const now = Date.now();
  const startLat = 34.0458 + seed * 0.0011;
  const startLon = -118.2588 + seed * 0.0014;

  return Array.from({ length: 18 }, (_, index) => ({
    timestamp: new Date(now - (17 - index) * 180_000).toISOString(),
    latitude: startLat + index * 0.00042 + Math.sin(index / 2.2) * 0.00022,
    longitude: startLon + index * 0.00083 + Math.cos(index / 2.7) * 0.0003,
    valid: gpsValid && !(index === 9 || index === 10),
  }));
}

function createNtc(seed: number): NtcChannel[] {
  const baseValues = [2584, 2468, 2215, 2738, 2382, 2674, 2518, 2896];
  return ntcNames.map((name, index) => {
    const raw = (baseValues[index] ?? 2500) + seed * 13 + index * 7;
    return {
      id: index + 1,
      name,
      raw,
      temperatureC: null,
      status: index === 4 && seed === 4 ? "warning" : "normal",
      updatedSeconds: 4 + index,
      history: Array.from({ length: 28 }, (_, pointIndex) => ({
        timestamp: new Date(Date.now() - (27 - pointIndex) * 20_000).toISOString(),
        raw: Math.round(raw + Math.sin((pointIndex + seed) / 3.8) * 52),
      })),
    };
  });
}

export function createMockBundle(device: CoolingDevice): DeviceTelemetryBundle {
  const seed = Number(device.id.split("-")[1]) || 1;
  const gpsValid = device.id !== "trc-003" && device.status !== "offline";
  const voltageHistory = createVoltageHistory(seed);
  const lastVoltage = voltageHistory.at(-1)?.voltageV ?? 24.52;
  const lastCurrent = voltageHistory.at(-1)?.currentA ?? 3.18;
  const gpsTrack = createGpsTrack(seed, gpsValid);
  const lastTrackPoint = gpsTrack.at(-1);
  const fault = device.status === "fault";

  const outputs = defaultOutputs.map((output) => {
    if (fault && output.id === "compressor") {
      return { ...output, status: "fault" as const, description: "Drive fault requires inspection" };
    }
    if (device.status === "offline") {
      return { ...output, status: "unknown" as const, description: `Last known: ${output.status === "active" ? "active" : "inactive"} before disconnect` };
    }
    return output;
  });

  return {
    telemetry: {
      deviceId: device.id,
      capturedAt: new Date(Date.now() - device.lastReportSeconds * 1000).toISOString(),
      healthScore: fault ? 54 : device.status === "offline" ? 0 : 87 - seed,
      healthStatus: fault ? "fault" : device.status === "delayed" ? "warning" : "normal",
      coolingLoadPercent: fault ? 92 : 78 - seed,
      rssiDbm: -66 - seed,
      busVoltageV: lastVoltage,
      busCurrentA: lastCurrent,
      vehicleInputV: Number((13.86 - seed * 0.02).toFixed(2)),
      vehicleCharging: device.status !== "offline",
      pumpDutyPercent: device.status === "offline" ? 0 : 30,
      gps: {
        latitude: lastTrackPoint?.latitude ?? 34.0522,
        longitude: lastTrackPoint?.longitude ?? -118.2437,
        accuracyM: 8 + seed,
        valid: gpsValid,
        updatedSeconds: gpsValid ? 12 : 84,
      },
      outputs,
      ntcChannels: createNtc(seed),
    },
    voltageHistory,
    gpsTrack,
    rssiHistory: seededWave(seed, 18, -67 - seed, 3.2),
    voltageSparkline: voltageHistory.slice(-18).map((point) => point.voltageV),
    routeStats: {
      distanceKm: 28.4,
      durationMinutes: 52,
      averageSpeedKmh: 32.7,
    },
  };
}
