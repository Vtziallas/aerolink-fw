import type { VehicleState } from "../types";
import type { LinkStatus } from "../hooks/useTelemetry";

function fmt(value: number | null | undefined, digits = 1, unit = ""): string {
  if (value == null || Number.isNaN(value)) return "--";
  return `${value.toFixed(digits)}${unit}`;
}

export function TelemetryPanel({
  state,
  linkStatus,
}: {
  state: VehicleState | null;
  linkStatus: LinkStatus;
}) {
  return (
    <div className="telemetry-panel">
      <div className="telemetry-row telemetry-header">
        <span className={`link-badge link-${linkStatus}`}>{linkStatus.toUpperCase()}</span>
        <span className={`armed-badge ${state?.armed ? "armed" : "disarmed"}`}>
          {state?.armed ? "ARMED" : "DISARMED"}
        </span>
        <span className="flight-mode">{state?.flight_mode ?? "--"}</span>
      </div>

      <div className="telemetry-grid">
        <Field label="Altitude (rel)" value={fmt(state?.relative_altitude_m, 1, " m")} />
        <Field label="Altitude (abs)" value={fmt(state?.absolute_altitude_m, 1, " m")} />
        <Field label="Airspeed" value={fmt(state?.airspeed_m_s, 1, " m/s")} />
        <Field label="Groundspeed" value={fmt(state?.groundspeed_m_s, 1, " m/s")} />
        <Field label="Heading" value={fmt(state?.heading_deg, 0, "°")} />
        <Field label="Roll" value={fmt(state?.roll_deg, 1, "°")} />
        <Field label="Pitch" value={fmt(state?.pitch_deg, 1, "°")} />
        <Field label="Battery" value={fmt(state?.battery_remaining_pct, 0, "%")} />
        <Field label="Battery V" value={fmt(state?.battery_voltage_v, 1, " V")} />
        <Field
          label="GPS"
          value={state?.is_gps_ok ? "OK" : "NO FIX"}
          warn={!state?.is_gps_ok}
        />
      </div>
    </div>
  );
}

function Field({ label, value, warn }: { label: string; value: string; warn?: boolean }) {
  return (
    <div className="telemetry-field">
      <div className="telemetry-label">{label}</div>
      <div className={`telemetry-value ${warn ? "warn" : ""}`}>{value}</div>
    </div>
  );
}
