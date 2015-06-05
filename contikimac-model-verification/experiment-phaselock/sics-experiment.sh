#!/bin/sh

FILES=(setup.txt merged-log.txt sender.txt receiver.txt \
	interferer.txt rssi-scanner.txt)

rm -rf ${FILES[*]}

INTERFERER_PORT=14
#SCANNER_PORT=13
RECEIVER_PORT=15
SENDER_PORT=16

DIR=`date +"experiment_%F_%T"`

WAIT_TIME=10
MEASUREMENT_TIME=1800

echo "Resetting the testbed..."
make sky-reset
make sky-reset
make sky-reset

echo "Starting the RSSI scanner and the interferer"
echo "Running for" $WAIT_TIME "seconds..."

make login CMOTES=$INTERFERER_PORT |./timestamp > interferer.txt &
#make login CMOTES=$SCANNER_PORT > rssi-scanner.txt &

sleep $WAIT_TIME
echo "Starting the sender and the reader"

make sky-reset
make sky-reset
make sky-reset

make login CMOTES=$SENDER_PORT |./timestamp > sender.txt &
make login CMOTES=$RECEIVER_PORT |./timestamp > receiver.txt &

echo "Measuring for" $MEASUREMENT_TIME "seconds..."
sleep $MEASUREMENT_TIME

echo "Terminating the experiment..."
skill -c make

sleep 1
echo "Moving the files into the directory" $DIR
cat sender.txt receiver.txt | sort -n > merged-log.txt

echo "Experimental setup: " > setup.txt
echo "WAIT_TIME = " $WAIT_TIME >> setup.txt
echo "MEASUREMENT_TIME = " $MEASUREMENT_TIME >> setup.txt
echo "Project Configuration Header:" >> setup.txt
cat project-conf.h >> setup.txt

mkdir $DIR && mv ${FILES[*]} $DIR
cp net-test.c $DIR
../scripts/analyze-log-serial.py $DIR/merged-log.txt | tee $DIR/stats.txt

