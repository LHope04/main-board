import { describe, expect, it } from "vitest";
import { deriveRouteStats, mapDevice, mapRssiToDbm, mapTelemetryBundle } from "@/services/telemetry-mappers";
import type { ApiDeviceRow, ApiTelemetryRow } from "@/types/api";

const now = Date.parse("2026-07-22T09:00:00.000Z");

function deviceRow(lastSeenSecondsAgo: number): ApiDeviceRow {
  return {
    id: 1,
    sn: "UPB-TEST-001",
    name: "Test cooling unit",
    model: null,
    last_seen_at: new Date(now - lastSeenSecondsAgo * 1000).toISOString(),
    latest_received_at: null,
    latest_seq: 1,
    latest_rssi: 22,
    latest_latitude: 29.5,
    latest_longitude: 106.5,
    latest_gps_fix: true,
    latest_raw_json: null,
  };
}

function telemetryRow(overrides: Partial<ApiTelemetryRow> = {}): ApiTelemetryRow {
  return {
    id: 1,
    sn: "UPB-TEST-001",
    seq: 1,
    received_at: new Date(now - 4_000).toISOString(),
    rssi: 22,
    latitude: 29.5,
    longitude: 106.5,
    gps_fix: true,
    ntc_raw: [100, 200, 300, 400, 500, 600, 700, 800],
    bat24_v: 24.2,
    bat24_i: 2,
    v12_v: 13.6,
    boost_on: true,
    load_on: true,
    fan_on: false,
    pump_on: null,
    compressor_on: true,
    raw_json: { gps: { accuracy_m: 7 } },
    ...overrides,
  };
}

describe("telemetry mappers", () => {
  it("applies the agreed online, delayed and offline thresholds", () => {
    expect(mapDevice(deviceRow(10), now).status).toBe("online");
    expect(mapDevice(deviceRow(11), now).status).toBe("delayed");
    expect(mapDevice(deviceRow(60), now).status).toBe("delayed");
    expect(mapDevice(deviceRow(61), now).status).toBe("offline");
  });

  it("converts modem CSQ values while preserving native dBm", () => {
    expect(mapRssiToDbm(22)).toBe(-69);
    expect(mapRssiToDbm(-83)).toBe(-83);
    expect(mapRssiToDbm(99)).toBeNull();
  });

  it("keeps unavailable backend values explicit", () => {
    const device = mapDevice(deviceRow(4), now);
    const row = telemetryRow({ bat24_i: null, v12_v: null, pump_on: null, raw_json: null });
    const bundle = mapTelemetryBundle(device, row, [row], [], []);
    expect(bundle.telemetry.coolingLoadPercent).toBeNull();
    expect(bundle.telemetry.vehicleCharging).toBeNull();
    expect(bundle.telemetry.outputs.find((output) => output.id === "pump")?.status).toBe("unknown");
    expect(bundle.telemetry.gps.accuracyM).toBeNull();
    expect(bundle.telemetry.ntcChannels[0]?.temperatureC).toBeNull();
  });

  it("does not present last-known actuator values as live when the device is offline", () => {
    const device = { ...mapDevice(deviceRow(4), now), status: "offline" as const };
    const row = telemetryRow({ boost_on: true, fan_on: false });
    const bundle = mapTelemetryBundle(device, row, [row], [], []);
    expect(bundle.telemetry.outputs.every((output) => output.status === "unknown")).toBe(true);
    expect(bundle.telemetry.outputs.find((output) => output.id === "boost")?.description).toBe("Last known: active before disconnect");
    expect(bundle.telemetry.outputs.find((output) => output.id === "fan")?.description).toBe("Last known: inactive before disconnect");
  });

  it("derives load, route statistics and current telemetry from backend rows", () => {
    const device = mapDevice(deviceRow(4), now);
    const first = telemetryRow({ received_at: "2026-07-22T08:00:00.000Z" });
    const second = telemetryRow({ received_at: "2026-07-22T08:10:00.000Z", latitude: 29.51, longitude: 106.51 });
    const gps = [first, second].map((row) => ({
      received_at: row.received_at,
      seq: row.seq,
      latitude: row.latitude!,
      longitude: row.longitude!,
      gps_fix: row.gps_fix,
      raw_json: row.raw_json,
    }));
    const bundle = mapTelemetryBundle(device, second, [first, second], gps, []);
    expect(bundle.telemetry.coolingLoadPercent).toBe(40);
    expect(bundle.telemetry.rssiDbm).toBe(-69);
    expect(bundle.routeStats?.distanceKm).toBeGreaterThan(1);
    expect(bundle.routeStats?.durationMinutes).toBe(10);
    expect(deriveRouteStats(bundle.gpsTrack)?.averageSpeedKmh).toBeGreaterThan(0);
  });
});
