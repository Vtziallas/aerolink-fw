import type { Waypoint } from "./types";

const BACKEND_HTTP_URL = import.meta.env.VITE_BACKEND_HTTP_URL ?? "http://localhost:8000";

export class ApiError extends Error {
  errors: string[];
  constructor(errors: string[]) {
    super(errors.join("; "));
    this.errors = errors;
  }
}

async function postJson<T>(path: string, body?: unknown): Promise<T> {
  const response = await fetch(`${BACKEND_HTTP_URL}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: body != null ? JSON.stringify(body) : undefined,
  });

  if (!response.ok) {
    const payload = await response.json().catch(() => null);
    const errors: string[] = payload?.detail?.errors ?? [`Request failed (${response.status})`];
    throw new ApiError(errors);
  }

  return response.json() as Promise<T>;
}

export function uploadMission(aircraftId: string, waypoints: Waypoint[]) {
  return postJson<{ status: string; waypoint_count: number }>("/api/missions", {
    aircraft_id: aircraftId,
    waypoints,
  });
}

export function startMission() {
  return postJson<{ status: string }>("/api/missions/current/start");
}

export function returnToLaunch() {
  return postJson<{ status: string }>("/api/emergency/rtl");
}

export interface GcsConfig {
  home_lat_deg: number;
  home_lon_deg: number;
  geofence_radius_m: number;
  min_altitude_m: number;
  max_altitude_m: number;
}

export async function getConfig(): Promise<GcsConfig> {
  const response = await fetch(`${BACKEND_HTTP_URL}/api/config`);
  if (!response.ok) throw new ApiError([`Failed to load config (${response.status})`]);
  return response.json() as Promise<GcsConfig>;
}
