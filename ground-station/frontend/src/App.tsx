import { MapView } from "./components/MapView";
import { TelemetryPanel } from "./components/TelemetryPanel";
import { useTelemetry } from "./hooks/useTelemetry";
import "./App.css";

function App() {
  const { state, linkStatus } = useTelemetry();

  return (
    <div className="app">
      <header className="app-header">AeroLink-FW Ground Station</header>
      <div className="app-body">
        <MapView state={state} />
        <TelemetryPanel state={state} linkStatus={linkStatus} />
      </div>
    </div>
  );
}

export default App;
