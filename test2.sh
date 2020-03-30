#!/usr/bin/env bash
echo "compiling"
./tworker "6099" "5003" & p1=$!
./tworker "8888" "5004" & p2=$!
./tmanager "2343" & p3=$!
wait $p1 $p2 $p3
./cmd begin localhost 6099 localhost 2343 1001
./cmd newa localhost 6099 5
./cmd newb localhost 6099 3
./cmd join localhost 8888 localhost 2343 1001
./cmd newa localhost 8888 12
./cmd newb localhost 8888 23
killall -9 $p3
./cmd commit localhost 6099
./tmanager "2343" & p4 = $!
wait $p1 $p2 $p4
killall -9 $p1
killall -9 $p2
killall -9 $p4
