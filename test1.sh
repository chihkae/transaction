#!/usr/bin/env bash
echo "Test 1"
./cmd begin localhost 1111 localhost 3333 1001
./cmd newa localhost 1111 5
./cmd newb localhost 1111 3
./cmd join localhost 7777 localhost 3333 1001
./cmd newa localhost 7777 12
./cmd newb localhost 7777 23
./cmd commit localhost 1111
