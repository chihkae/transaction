#!/usr/bin/env bash
echo "Test 3"
./tworker "5333" "5041" & p1=$!
./tworker "2342" "5391" & p2=$!
./tmanager "5432" & p3=$!
sleep 5
./cmd begin localhost 5333 localhost 5432 1001
./cmd newa localhost 5333 5
./cmd newb localhost 5333 3
./cmd join localhost 2342 localhost 5432 1001
./cmd newa localhost 2342 12
./cmd newb localhost 2342 23
./cmd commitcrash localhost 5333
sleep 3
./tmanager "5432" & p3=$!
sleep 8
kill $p1
wait $p1
kill $p2
wait $p2
kill $p3
wait $p3
