#!/usr/bin/env bash
echo "compiling"
./tworker "5053" "5059" & p1=$!
./tworker "5023" "5399" & p2=$!
./tmanager "9871" & p3=$!
wait $p1 $p2 $p3
./cmd begin localhost 5053 localhost 9871 1001
./cmd newa localhost 5053 5
./cmd newb localhost 5053 3
./cmd join localhost 5023 localhost 9871 1001
./cmd newa localhost 5023 12
./cmd newb localhost 5023 23
killall -9 $p3
./cmd commit localhost 5053
./tmanager "9871" & p4 = $!
wait $p1 $p2 $p4 $p3
killall -9 $p1
killall -9 $p2
killall -9 $p4
