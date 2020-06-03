#!/bin/bash

make

./tworker "2003" "2004" &
P1=$!
./tworker "2005" "2006" &
P2=$!
./tmanager "2007"&
P3=$!
wait $P1 $P2 $P3
./cmd begin localhost 2003 localhost 2007 1001
./cmd newa localhost 2003 5
./cmd newb localhost 2003 3
./cmd join localhost 2005 localhost 2007 1001 
./cmd newa localhost 2005 12
./cmd newb localhost 2005 23

sleep 6
kill $P3
./cmd commit localhost 2003
sleep 5
./tmanager 2007
P3=$!

