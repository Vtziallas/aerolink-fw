#!/bin/bash
# Flies stress scenario S4 (150deg near-reversal turn, generous 300m/300m
# legs) through the real REST API. Requires launch-ground-stress-test.sh
# to be the currently-running backend (relaxed MAX_TURN_ANGLE_DEG). No
# sudo needed -- talks to the backend directly via its ground-net IP,
# same path the socat relay uses, avoiding netns exec entirely.

set -euo pipefail

echo "-- uploading mission"
curl -s -X POST http://10.201.0.2:8000/api/missions \
  -H "Content-Type: application/json" \
  -d '{
    "aircraft_id": "stress-test",
    "waypoints": [
      {"latitude_deg": 47.4004400, "longitude_deg": 8.5455930, "altitude_m": 30, "speed_m_s": 15},
      {"latitude_deg": 47.3981034, "longitude_deg": 8.5475859, "altitude_m": 30, "speed_m_s": 15}
    ]
  }'
echo
echo "-- starting mission"
curl -s -X POST http://10.201.0.2:8000/api/missions/current/start
echo
