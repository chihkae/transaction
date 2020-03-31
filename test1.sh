#!/usr/bin/env bash
echo "Test 1"
./tworker "1111" "5059" & p1=$!
./tworker "7777" "5399" & p2=$!
./tmanager "3333" & p3=$!
sleep 5
./cmd begin localhost 1111 localhost 3333 1001
./cmd newa localhost 1111 5
./cmd newb localhost 1111 3
./cmd join localhost 7777 localhost 3333 1001
./cmd newa localhost 7777 12
./cmd newb localhost 7777 23
sleep 4
./cmd commit localhost 1111
kill $p1
wait $p1
kill $p2
wait $p2
kill $p3
wait $p3
