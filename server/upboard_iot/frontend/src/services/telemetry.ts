import { mockDevices } from "@/mocks/devices";
import { createMockBundle } from "@/mocks/telemetry";
import { requestJson } from "@/services/api-client";
import { mapDevice, mapTelemetryBundle } from "@/services/telemetry-mappers";
import type { ApiDeviceRow, ApiEventRow, ApiGpsRow, ApiTelemetryRow } from "@/types/api";
import type { CoolingDevice, DemoMode, DeviceTelemetryBundle } from "@/types/telemetry";

const wait = (milliseconds: number) =>
  new Promise<void>((resolve) => window.setTimeout(resolve, milliseconds));

export interface TelemetryDataSource {
  getDevices: (mode?: DemoMode) => Promise<CoolingDevice[]>;
  getDeviceBundle: (device: CoolingDevice, mode?: DemoMode) => Promise<DeviceTelemetryBundle>;
}

export const telemetryQueryKeys = {
  devices: (mode: DemoMode) => ["devices", mode] as const,
  bundle: (deviceId: string, mode: DemoMode) => ["device-bundle", deviceId, mode] as const,
};

export const mockTelemetryDataSource: TelemetryDataSource = {
  async getDevices(mode = "normal") {
    await wait(620);
    if (mode === "error") throw new Error("Telemetry service is temporarily unavailable.");
    if (mode === "empty") return [];
    return mockDevices;
  },

  async getDeviceBundle(device, mode = "normal") {
    await wait(520);
    if (mode === "error") throw new Error("Unable to load the selected device telemetry.");
    return createMockBundle(device);
  },
};

export const apiTelemetryDataSource: TelemetryDataSource = {
  async getDevices() {
    const response = await requestJson<{ devices: ApiDeviceRow[] }>("/api/devices");
    return response.devices.map((row) => mapDevice(row));
  },

  async getDeviceBundle(device) {
    const encodedDeviceId = encodeURIComponent(device.id);
    const [latestResponse, historyResponse, gpsResponse, eventsResponse] = await Promise.all([
      requestJson<{ latest: ApiTelemetryRow | null }>(`/api/devices/${encodedDeviceId}/latest`),
      requestJson<{ telemetry: ApiTelemetryRow[] }>(`/api/devices/${encodedDeviceId}/telemetry?limit=500`),
      requestJson<{ gps: ApiGpsRow[] }>(`/api/devices/${encodedDeviceId}/gps?limit=500`),
      requestJson<{ events: ApiEventRow[] }>(`/api/devices/${encodedDeviceId}/events?limit=200`),
    ]);
    return mapTelemetryBundle(
      device,
      latestResponse.latest,
      historyResponse.telemetry,
      gpsResponse.gps,
      eventsResponse.events,
    );
  },
};

export const isMockDataSource = import.meta.env.VITE_DATA_SOURCE === "mock";
export const telemetryDataSource: TelemetryDataSource = isMockDataSource
  ? mockTelemetryDataSource
  : apiTelemetryDataSource;
