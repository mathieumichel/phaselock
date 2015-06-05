#!/usr/bin/env python

import numpy, re, sys

def timestamp_to_ms(t):
    t_vector = re.split("\.|:", t)
    off = len(t_vector) - 1
    ms = int(t_vector[off])
    off -= 1
    if off >= 0:
        ms += int(t_vector[off]) * 1000
        off -= 1
    if off >= 0:
        ms += int(t_vector[off]) * 1000 * 60
        off -= 1
    if off >= 0:
        ms += int(t_vector[off]) * 1000 * 60 * 60
        off -= 1

    return ms

highest_tx_id = -1
max_msg_id = 10000

TX_Times = [-1] * max_msg_id
RX_Times = [-1] * max_msg_id
Latencies = []
Strobes = []

if len(sys.argv) != 2:
    print "Usage: analyze-log.py <log-file>"
    sys.exit(1)

log_file_name = sys.argv[1]

print "Analyzing the log", log_file_name, "..."

with open(log_file_name, "r") as log_file:
    for line in log_file.readlines():
        line_vector = line.strip().split()
        if len(line_vector) < 4:
            continue;
        timestamp = timestamp_to_ms(line_vector[0])
        msg_type = line_vector[1]
        if msg_type == "RX":
            try:
                rx_id = int(line_vector[4])
                if rx_id > max_msg_id:
                    print "RX id ", rx_id, "is too high"
                    sys.exit(1)
                RX_Times.insert(rx_id, timestamp)
#                print "RX id:", rx_id, "time:", RX_Times[rx_id]
            except ValueError:
                print "Failed to parse RX id", line_vector[4]
        elif msg_type == "TX":
            if line_vector[2] == "MSG":
                try:
                    tx_id = int(line_vector[3])
                    if tx_id > max_msg_id:
                        print "TX id", tx_id, "is too high"
                        sys.exit(1)
#                    print "TX id:", tx_id, "time:", timestamp
                    if tx_id > highest_tx_id:
                        highest_tx_id = tx_id
                    TX_Times.insert(tx_id, timestamp)
                except ValueError:
                    print "Failed to parse TX id", line_vector[3]
        elif msg_type == "STROBES":
            try:
                number_of_strobes = int(line_vector[2])
                Strobes.append(number_of_strobes)
            except ValueError:
                 print "Failed to parse strobe count", line_vector[2]

if len(RX_Times) > len(TX_Times):
    print "Error: Received more packets than what were sent"
    sys.exit(1)

lost_packets = 0
received_packets = 0


for i in range(0, highest_tx_id):
    if RX_Times[i] == -1:
        print "Packet", i, "was lost!"
        lost_packets += 1
    else:
        received_packets += 1
        Latencies.append(RX_Times[i] - TX_Times[i])

if len(Latencies) == 0 or lost_packets + received_packets == 0:
   print "Unable to generate statistical summary: no received packets!";
   sys.exit(1)

print "Mean latency:", numpy.mean(Latencies), "ms"
print "St.dev of latencies:", numpy.std(Latencies)

print "Sent packets:", lost_packets + received_packets;
print "Received packets:", received_packets;
print "PRR:", received_packets / float((lost_packets + received_packets))