// Mirrors app.vehicle.VehicleState.to_dict() on the backend (Phase 2 scope only).
export interface VehicleState {
  is_connected: boolean;

  latitude_deg: number | null;
  longitude_deg: number | null;
  absolute_altitude_m: number | null;
  relative_altitude_m: number | null;

  roll_deg: number | null;
  pitch_deg: number | null;
  yaw_deg: number | null;

  airspeed_m_s: number | null;
  groundspeed_m_s: number | null;
  heading_deg: number | null;

  battery_voltage_v: number | null;
  battery_remaining_pct: number | null;

  flight_mode: string | null;
  armed: boolean;

  is_gps_ok: boolean;
  is_armable: boolean;

  mission_uploaded: boolean;
  mission_current: number | null;
  mission_total: number | null;
}

// Mirrors app.mission.Waypoint on the backend.
export interface Waypoint {
  latitude_deg: number;
  longitude_deg: number;
  altitude_m: number;
}

