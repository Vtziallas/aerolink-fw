import { useEffect, useRef } from "react";
import { GeoJSONSource, MapLibreMap, Marker, NavigationControl } from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import type { StyleSpecification } from "@maplibre/maplibre-gl-style-spec";
import type { GcsConfig } from "../api";
import type { VehicleState, Waypoint } from "../types";

// PX4 SITL's default home position (Zurich) -- used only so the map has a
// sane starting view before /api/config or the first telemetry frame arrive.
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

const ROUTE_SOURCE_ID = "planned-route";
const GEOFENCE_SOURCE_ID = "geofence";

function makeAircraftMarkerElement(): HTMLDivElement {
  const el = document.createElement("div");
  el.className = "aircraft-marker";
  el.innerHTML = `<svg viewBox="0 0 24 24" width="28" height="28">
    <path d="M12 1 L18 20 L12 16 L6 20 Z" fill="#2e7d32" stroke="#0b2e10" stroke-width="1"/>
  </svg>`;
  return el;
}

function makeWaypointMarkerElement(index: number): HTMLDivElement {
  const el = document.createElement("div");
  el.className = "waypoint-marker";
  el.textContent = String(index + 1);
  return el;
}

function makeHomeMarkerElement(): HTMLDivElement {
  const el = document.createElement("div");
  el.className = "home-marker";
  el.innerHTML = `<svg viewBox="0 0 24 24" width="26" height="26">
    <path d="M12 2 L22 12 H18 V22 H6 V12 H2 Z" fill="#38bdf8" stroke="#0c4a6e" stroke-width="1.2"/>
  </svg>`;
  return el;
}

/** GeoJSON polygon approximating a circle of `radiusMeters` around a lng/lat point. */
function circlePolygon(centerLng: number, centerLat: number, radiusMeters: number, points = 64) {
  const coords: [number, number][] = [];
  const earthRadius = 6_371_000;
  for (let i = 0; i <= points; i++) {
    const angle = (i / points) * 2 * Math.PI;
    const dLat = (radiusMeters * Math.cos(angle)) / earthRadius;
    const dLng =
      (radiusMeters * Math.sin(angle)) / (earthRadius * Math.cos((centerLat * Math.PI) / 180));
    coords.push([centerLng + (dLng * 180) / Math.PI, centerLat + (dLat * 180) / Math.PI]);
  }
  return {
    type: "Feature" as const,
    properties: {},
    geometry: { type: "Polygon" as const, coordinates: [coords] },
  };
}

export function MapView({
  state,
  waypoints,
  onMapClick,
  homeConfig,
}: {
  state: VehicleState | null;
  waypoints: Waypoint[];
  onMapClick: (latitude_deg: number, longitude_deg: number) => void;
  homeConfig: GcsConfig | null;
}) {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<MapLibreMap | null>(null);
  const aircraftMarkerRef = useRef<Marker | null>(null);
  const homeMarkerRef = useRef<Marker | null>(null);
  const waypointMarkersRef = useRef<Marker[]>([]);
  const hasCenteredRef = useRef(false);
  const onMapClickRef = useRef(onMapClick);
  onMapClickRef.current = onMapClick;

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

    map.on("load", () => {
      map.addSource(ROUTE_SOURCE_ID, {
        type: "geojson",
        data: { type: "Feature", properties: {}, geometry: { type: "LineString", coordinates: [] } },
      });
      map.addLayer({
        id: ROUTE_SOURCE_ID,
        type: "line",
        source: ROUTE_SOURCE_ID,
        paint: { "line-color": "#facc15", "line-width": 2, "line-dasharray": [2, 2] },
      });

      map.addSource(GEOFENCE_SOURCE_ID, {
        type: "geojson",
        data: { type: "Feature", properties: {}, geometry: { type: "Polygon", coordinates: [[]] } },
      });
      map.addLayer({
        id: GEOFENCE_SOURCE_ID,
        type: "line",
        source: GEOFENCE_SOURCE_ID,
        paint: { "line-color": "#f87171", "line-width": 1.5, "line-dasharray": [4, 3] },
      });
    });

    map.on("click", (e) => onMapClickRef.current(e.lngLat.lat, e.lngLat.lng));

    aircraftMarkerRef.current = new Marker({ element: makeAircraftMarkerElement() })
      .setLngLat(DEFAULT_CENTER)
      .addTo(map);

    return () => {
      map.remove();
      mapRef.current = null;
      aircraftMarkerRef.current = null;
      homeMarkerRef.current = null;
      waypointMarkersRef.current = [];
    };
  }, []);

  useEffect(() => {
    const marker = aircraftMarkerRef.current;
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

  useEffect(() => {
    const map = mapRef.current;
    if (!map || !homeConfig) return;

    const homeLngLat: [number, number] = [homeConfig.home_lon_deg, homeConfig.home_lat_deg];

    if (!homeMarkerRef.current) {
      homeMarkerRef.current = new Marker({ element: makeHomeMarkerElement(), anchor: "bottom" })
        .setLngLat(homeLngLat)
        .addTo(map);
    } else {
      homeMarkerRef.current.setLngLat(homeLngLat);
    }

    const applyGeofence = () => {
      const source = map.getSource<GeoJSONSource>(GEOFENCE_SOURCE_ID);
      if (source) {
        source.setData(
          circlePolygon(homeConfig.home_lon_deg, homeConfig.home_lat_deg, homeConfig.geofence_radius_m)
        );
      }
    };
    if (map.isStyleLoaded()) applyGeofence();
    else map.once("load", applyGeofence);

    if (!hasCenteredRef.current) {
      map.setCenter(homeLngLat);
    }
  }, [homeConfig]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    for (const marker of waypointMarkersRef.current) marker.remove();
    waypointMarkersRef.current = waypoints.map((wp, i) =>
      new Marker({ element: makeWaypointMarkerElement(i) })
        .setLngLat([wp.longitude_deg, wp.latitude_deg])
        .addTo(map)
    );

    const source = map.getSource<GeoJSONSource>(ROUTE_SOURCE_ID);
    if (source) {
      source.setData({
        type: "Feature",
        properties: {},
        geometry: {
          type: "LineString",
          coordinates: waypoints.map((wp) => [wp.longitude_deg, wp.latitude_deg]),
        },
      });
    }
  }, [waypoints]);

  return <div ref={containerRef} style={{ position: "absolute", inset: 0 }} />;
}
