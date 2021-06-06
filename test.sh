#!/bin/bash

# ./bin/zbid_client user ics53 127.0.0.1 2000
for i in {1..100}
do
    ( sleep 0; printf "/c water,2,${i}\n/a\n" ) | ./bin/zbid_client "user${i}" "ics${i}" 127.0.0.1 2000 &
    # ( sleep 0; printf "/a\n" ) | ./bin/zbid_client user ics53 127.0.0.1 2000 &
done
# ( sleep 1; echo "/a" ) | ./bin/zbid_client user1 ics53 127.0.0.1 2000

