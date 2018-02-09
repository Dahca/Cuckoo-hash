#! /bin/sh
DIR=$(dirname $0)
COUNT=500000

echo "Running the test for $COUNT elements"
$DIR/cuckoo_hash 0 $COUNT
