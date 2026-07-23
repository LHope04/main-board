import type { ApiLoginResponse, ApiSession } from "@/types/api";

export class ApiError extends Error {
  constructor(
    message: string,
    public readonly status: number,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

function errorMessage(body: unknown, fallback: string) {
  if (body && typeof body === "object" && "detail" in body && typeof body.detail === "string") {
    return body.detail;
  }
  return fallback;
}

export async function requestJson<T>(input: string, init?: RequestInit): Promise<T> {
  const headers = new Headers(init?.headers);
  if (init?.body && !headers.has("Content-Type")) headers.set("Content-Type", "application/json");

  const response = await fetch(input, {
    ...init,
    headers,
    credentials: "same-origin",
  });
  const body = response.status === 204 ? null : await response.json().catch(() => null);

  if (!response.ok) {
    if (response.status === 401) window.dispatchEvent(new Event("thermoride:unauthorized"));
    throw new ApiError(errorMessage(body, `Request failed with status ${response.status}.`), response.status);
  }
  return body as T;
}

export function getSession() {
  return requestJson<ApiSession>("/api/me");
}

export function login(username: string, password: string) {
  return requestJson<ApiLoginResponse>("/api/auth/login", {
    method: "POST",
    body: JSON.stringify({ username, password }),
  });
}

export function logout() {
  return requestJson<{ ok: boolean }>("/api/auth/logout", { method: "POST" });
}
