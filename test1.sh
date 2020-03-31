#!/usr/bin/env bash
echo "Test 1"
./tworker "1112" "5059" & p1=$!
./tworker "7778" "5399" & p2=$!
./tmanager "3334" & p3=$!
sleep 5
./cmd begin localhost 1112 localhost 3334 1001
./cmd newa localhost 1112 5
./cmd newb localhost 1112 3
./cmd join localhost 7778 localhost 3334 1001
./cmd newa localhost 7778 12
./cmd newb localhost 7778 23
sleep 4
./cmd commit localhost 1112
sleep 4
kill $p1
wait $p1
kill $p2
wait $p2
kill $p3
wait $p3
