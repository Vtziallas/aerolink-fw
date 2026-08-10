import { useState } from "react";
import type { VehicleState, Waypoint } from "../types";
import { ApiError, startMission, uploadMission } from "../api";

const DEFAULT_ALTITUDE_M = 30;

export function MissionPanel({
  waypoints,
  onRemoveLast,
  onClear,
  state,
}: {
  waypoints: Waypoint[];
  onRemoveLast: () => void;
  onClear: () => void;
  state: VehicleState | null;
}) {
  const [errors, setErrors] = useState<string[]>([]);
  const [status, setStatus] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const missionUploaded = state?.mission_uploaded ?? false;

  const handleUpload = async () => {
    setBusy(true);
    setErrors([]);
    setStatus(null);
    try {
      const result = await uploadMission("sitl-01", waypoints);
      setStatus(`Uploaded ${result.waypoint_count} waypoint(s)`);
    } catch (e) {
      setErrors(e instanceof ApiError ? e.errors : ["Upload failed"]);
    } finally {
      setBusy(false);
    }
  };

  const handleStart = async () => {
    setBusy(true);
    setErrors([]);
    setStatus(null);
    try {
      await startMission();
      setStatus("Mission started");
    } catch (e) {
      setErrors(e instanceof ApiError ? e.errors : ["Start failed"]);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="mission-panel">
      <h3>Mission Plan</h3>
      <div className="mission-panel-hint">Click the map to add a waypoint (alt. {DEFAULT_ALTITUDE_M} m)</div>

      <ul className="waypoint-list">
        {waypoints.map((wp, i) => (
          <li key={i}>
            {i + 1}. {wp.latitude_deg.toFixed(5)}, {wp.longitude_deg.toFixed(5)} @ {wp.altitude_m} m
          </li>
        ))}
      </ul>

      <button onClick={onRemoveLast} disabled={busy || waypoints.length === 0}>
        Remove last
      </button>
      <button onClick={onClear} disabled={busy || waypoints.length === 0}>
        Clear all
      </button>
      <button className="primary" onClick={handleUpload} disabled={busy || waypoints.length === 0}>
        Validate &amp; Upload Mission
      </button>
      <button className="primary" onClick={handleStart} disabled={busy || !missionUploaded}>
        Start Mission
      </button>

      {state?.mission_total != null && state.mission_total > 0 && (
        <div className="mission-status">
          Progress: {state.mission_current}/{state.mission_total}
        </div>
      )}
      {status && <div className="mission-status">{status}</div>}
      {errors.length > 0 && (
        <ul className="mission-errors">
          {errors.map((e, i) => (
            <li key={i}>{e}</li>
          ))}
        </ul>
      )}
    </div>
  );
}

export { DEFAULT_ALTITUDE_M };
