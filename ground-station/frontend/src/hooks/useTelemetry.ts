import { useEffect, useRef, useState } from "react";
import type { VehicleState } from "../types";

export type LinkStatus = "connecting" | "open" | "closed";

const BACKEND_WS_URL =
  import.meta.env.VITE_BACKEND_WS_URL ?? "ws://localhost:8000/ws/telemetry";

const RECONNECT_DELAY_MS = 1000;

// Phase 2 scope: a straightforward reconnecting WebSocket client for the
// telemetry stream. This is deliberately not the final link-status story --
// see SAFETY.md / NETWORKING.md for how LTE loss is actually meant to be
// handled onboard. This hook just needs to survive the backend restarting
// or a dev-server reload without the UI getting stuck.
export function useTelemetry(): { state: VehicleState | null; linkStatus: LinkStatus } {
  const [state, setState] = useState<VehicleState | null>(null);
  const [linkStatus, setLinkStatus] = useState<LinkStatus>("connecting");
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    let cancelled = false;
    let socket: WebSocket | null = null;

    const connect = () => {
      if (cancelled) return;
      setLinkStatus("connecting");
      socket = new WebSocket(BACKEND_WS_URL);

      socket.onopen = () => setLinkStatus("open");

      socket.onmessage = (event) => {
        setState(JSON.parse(event.data) as VehicleState);
      };

      socket.onclose = () => {
        if (cancelled) return;
        setLinkStatus("closed");
        reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
      };

      socket.onerror = () => {
        socket?.close();
      };
    };

    connect();

    return () => {
      cancelled = true;
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      socket?.close();
    };
  }, []);

  return { state, linkStatus };
}
