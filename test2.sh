#!/usr/bin/env bash
echo "compiling"
./tworker "6034" "6035" & p1=$!
./tworker "6053" "6059" & p2=$!
./tmanager "4023" & p3=$!
wait $p1 $p2 $p3
./cmd begin localhost 6034 localhost 4023 1001
./cmd newa localhost 6034 5
./cmd newb localhost 6034 3
./cmd join localhost 6053 localhost 4023 1001
./cmd newa localhost 6053 12
./cmd newb localhost 6053 23
killall -9 $p3
./cmd commit localhost 6034
./tmanager "4023" & p4 = $!
wait $p1 $p2 $p4 $p3
killall -9 $p1
killall -9 $p2
killall -9 $p4
