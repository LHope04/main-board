export interface ApiSession {
  authenticated: boolean;
  username?: string;
  role?: string;
}

export interface ApiLoginResponse {
  ok: boolean;
  username: string;
  role: string;
}

export interface ApiPumpControlResponse {
  ok: boolean;
  device: string;
  topic: string;
  duty_pct: number;
  message_id: number;
}

export interface ApiDeviceRow {
  id: number | string;
  sn: string;
  name: string | null;
  model: string | null;
  last_seen_at: string | null;
  latest_received_at: string | null;
  latest_seq: number | null;
  latest_rssi: number | null;
  latest_latitude: number | null;
  latest_longitude: number | null;
  latest_gps_fix: boolean | null;
  latest_raw_json: Record<string, unknown> | null;
}

export interface ApiTelemetryRow {
  id: number | string;
  sn: string;
  seq: number | null;
  received_at: string;
  rssi: number | null;
  latitude: number | null;
  longitude: number | null;
  gps_fix: boolean | null;
  ntc_raw: number[] | null;
  bat24_v: number | null;
  bat24_i: number | null;
  v12_v: number | null;
  boost_on: boolean | null;
  load_on: boolean | null;
  fan_on: boolean | null;
  pump_on: boolean | null;
  pump_duty_pct: number | null;
  compressor_on: boolean | null;
  raw_json: Record<string, unknown> | null;
}

export interface ApiGpsRow {
  received_at: string;
  seq: number | null;
  latitude: number;
  longitude: number;
  gps_fix: boolean | null;
  raw_json: Record<string, unknown> | null;
}

export interface ApiEventRow {
  received_at: string;
  level: string;
  event_type: string;
  message: string;
  raw_json: Record<string, unknown> | null;
}

export interface ApiRealtimeEvent {
  type: "telemetry" | "event";
  sn: string;
  data: Record<string, unknown>;
}
