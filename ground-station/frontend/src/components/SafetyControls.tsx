import { useState } from "react";
import type { VehicleState } from "../types";
import { ApiError, returnToLaunch } from "../api";

// Deliberately separate from MissionPanel: this is an emergency recall
// control, independent of whatever mission state exists, per SAFETY.md's
// "normal control vs. safety override" distinction.
export function SafetyControls({ state }: { state: VehicleState | null }) {
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<string | null>(null);

  const handleRtl = async () => {
    setBusy(true);
    setMessage(null);
    try {
      await returnToLaunch();
      setMessage("Returning to launch");
    } catch (e) {
      setMessage(e instanceof ApiError ? e.errors.join("; ") : "RTL failed");
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="safety-controls">
      <button
        className="rtl-button"
        onClick={handleRtl}
        disabled={busy || !state?.is_connected || !state?.armed}
      >
        RETURN TO LAUNCH
      </button>
      {message && <div className="safety-message">{message}</div>}
    </div>
  );
}
