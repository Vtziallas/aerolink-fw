import { useEffect, useState } from "react";
import { MapView } from "./components/MapView";
import { TelemetryPanel } from "./components/TelemetryPanel";
import { MissionPanel, DEFAULT_ALTITUDE_M } from "./components/MissionPanel";
import { SafetyControls } from "./components/SafetyControls";
import { useTelemetry } from "./hooks/useTelemetry";
import { getConfig, type GcsConfig } from "./api";
import type { Waypoint } from "./types";
import "./App.css";

function App() {
  const { state, linkStatus } = useTelemetry();
  const [waypoints, setWaypoints] = useState<Waypoint[]>([]);
  const [gcsConfig, setGcsConfig] = useState<GcsConfig | null>(null);

  useEffect(() => {
    getConfig()
      .then(setGcsConfig)
      .catch(() => {
        /* Map falls back to its default center; non-fatal. */
      });
  }, []);

  const addWaypoint = (latitude_deg: number, longitude_deg: number) => {
    setWaypoints((prev) => [...prev, { latitude_deg, longitude_deg, altitude_m: DEFAULT_ALTITUDE_M }]);
  };

  return (
    <div className="app">
      <header className="app-header">AeroLink-FW Ground Station</header>
      <div className="app-body">
        <MapView state={state} waypoints={waypoints} onMapClick={addWaypoint} homeConfig={gcsConfig} />
        <TelemetryPanel state={state} linkStatus={linkStatus} />
        <MissionPanel
          waypoints={waypoints}
          onRemoveLast={() => setWaypoints((prev) => prev.slice(0, -1))}
          onClear={() => setWaypoints([])}
          state={state}
        />
        <SafetyControls state={state} />
      </div>
    </div>
  );
}

export default App;
