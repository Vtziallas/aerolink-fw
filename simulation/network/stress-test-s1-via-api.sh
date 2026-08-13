#!/bin/bash
# Flies stress scenario S1 (89deg turn, 250m/280m legs -- the exact turn
# severity that broke fixed-wing in Phase 3) through the REAL REST API,
# not a bypass script. Only works while the backend is running via
# launch-ground-stress-test.sh (relaxed MAX_TURN_ANGLE_DEG/
# MIN_WAYPOINT_SEPARATION_M) -- the normal launch-ground.sh would reject
# this mission with a 422, which is the correct/intended behavior normally.
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

echo "-- uploading mission"
ip netns exec ground-net curl -s -X POST http://localhost:8000/api/missions \
  -H "Content-Type: application/json" \
  -d '{
    "aircraft_id": "stress-test",
    "waypoints": [
      {"latitude_deg": 47.3999903, "longitude_deg": 8.5455930, "altitude_m": 30, "speed_m_s": 15},
      {"latitude_deg": 47.4000342, "longitude_deg": 8.5493126, "altitude_m": 30, "speed_m_s": 15}
    ]
  }'
echo
echo "-- starting mission"
ip netns exec ground-net curl -s -X POST http://localhost:8000/api/missions/current/start
echo
