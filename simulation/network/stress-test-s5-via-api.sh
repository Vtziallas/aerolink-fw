#!/bin/bash
# Flies stress scenario S5 (overspeed cruise: 250m/250m legs, 40deg turn,
# 25 m/s vs. this airframe's 15 m/s FW_AIRSPD_TRIM) through the real REST
# API. Requires launch-ground-stress-test.sh to be the currently-running
# backend. No sudo needed -- talks to the backend directly via its
# ground-net IP (10.201.0.2), same path the socat relay uses.

set -euo pipefail

echo "-- uploading mission"
curl -s -X POST http://10.201.0.2:8000/api/missions \
  -H "Content-Type: application/json" \
  -d '{
    "aircraft_id": "stress-test",
    "waypoints": [
      {"latitude_deg": 47.3999903, "longitude_deg": 8.5455930, "altitude_m": 30, "speed_m_s": 25},
      {"latitude_deg": 47.4017126, "longitude_deg": 8.5477281, "altitude_m": 30, "speed_m_s": 25}
    ]
  }'
echo
echo "-- starting mission"
curl -s -X POST http://10.201.0.2:8000/api/missions/current/start
echo
