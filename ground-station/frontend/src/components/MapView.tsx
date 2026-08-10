import { useEffect, useRef } from "react";
import { MapLibreMap, Marker, NavigationControl } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import type { StyleSpecification } from "@maplibre/maplibre-gl-style-spec";
import type { VehicleState } from "../types";

// PX4 SITL's default home position (Zurich) -- used only so the map has a
// sane starting view before the first telemetry frame arrives.
const DEFAULT_CENTER: [number, number] = [8.545593, 47.397742];

// Free OSM raster tiles for local development. Fine for Phase 2; a demo-scale
// public deployment should move to a provider with an API key (MapTiler,
// Stadia, etc.) to respect OSM's usage policy -- not needed to make progress now.
const OSM_STYLE: StyleSpecification = {
  version: 8,
  sources: {
    osm: {
      type: "raster",
      tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
      tileSize: 256,
      attribution: "&copy; OpenStreetMap contributors",
    },
  },
  layers: [{ id: "osm", type: "raster", source: "osm" }],
};

export function MapView({ state }: { state: VehicleState | null }) {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<MapLibreMap | null>(null);
  const markerRef = useRef<Marker | null>(null);
  const hasCenteredRef = useRef(false);

  useEffect(() => {
    if (!containerRef.current) return;

    const map = new MapLibreMap({
      container: containerRef.current,
      style: OSM_STYLE,
      center: DEFAULT_CENTER,
      zoom: 15,
    });
    map.addControl(new NavigationControl(), "top-right");
    mapRef.current = map;

    const el = document.createElement("div");
    el.className = "aircraft-marker";
    el.innerHTML = `<svg viewBox="0 0 24 24" width="28" height="28">
      <path d="M12 1 L18 20 L12 16 L6 20 Z" fill="#2e7d32" stroke="#0b2e10" stroke-width="1"/>
    </svg>`;
    markerRef.current = new Marker({ element: el }).setLngLat(DEFAULT_CENTER).addTo(map);

    return () => {
      map.remove();
      mapRef.current = null;
      markerRef.current = null;
    };
  }, []);

  useEffect(() => {
    const marker = markerRef.current;
    const map = mapRef.current;
    if (!marker || !map || state?.latitude_deg == null || state?.longitude_deg == null) return;

    const lngLat: [number, number] = [state.longitude_deg, state.latitude_deg];
    marker.setLngLat(lngLat);
    if (state.heading_deg != null) {
      marker.setRotation(state.heading_deg);
    }

    if (!hasCenteredRef.current) {
      map.setCenter(lngLat);
      hasCenteredRef.current = true;
    }
  }, [state]);

  return <div ref={containerRef} style={{ position: "absolute", inset: 0 }} />;
}
