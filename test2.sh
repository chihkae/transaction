#!/usr/bin/env bash
echo "Test 2"
./tworker "5053" "5059" & p1=$!
./tworker "5023" "5399" & p2=$!
./tmanager "9871" & p3=$!
sleep 5
./cmd begin localhost 5053 localhost 9871 1001
./cmd newa localhost 5053 5
./cmd newb localhost 5053 3
./cmd join localhost 5023 localhost 9871 1001
./cmd newa localhost 5023 12
./cmd newb localhost 5023 23
kill $p3
wait $p3
./cmd commit localhost 5053
./tmanager "9871" & p4=$!
sleep 5
kill $p1
wait $p1
kill $p2
wait $p2
kill $p4
wait $p4
