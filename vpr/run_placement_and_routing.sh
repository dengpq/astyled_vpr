#!/bin/bash
echo "$DATE"

./bin/vpr e64-4x4lut.net 4x4lut_sanitized.arch e64-4x4lut.place e64-4x4lut.route -nodisp > \
    place_and_route_result/e64-4x4lut_place_and_route_result_$1.txt
