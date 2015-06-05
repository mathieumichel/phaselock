#!/usr/bin/python
import random
import math
import matplotlib.pyplot as plt
import sys

#number of slots with slot time 23.63 us and 250 kbit/s
#5 Byte = 7 slots (6.77)
#40 Byte = 54 slots

#import collections
from collections import Counter
#f = open('rssis', 'r')
#print f[1]

with open('rssi-binary.txt') as f:
    content = f.readlines()
#counter=collections.Counter(content)
c=Counter(content)
x= c.values()
print x[0]
print x[1]
samples=x[0]+x[1]
# Set lbyte to the expected packet length. Acks are typically 5 bytes.
# Minimum ContikiMAC data packet size is 43 bytes.
#lbyte = 5.0
lbyte = 43.0
timebit = 1.0/250000.0
timebytes = lbyte*8.0*timebit
#slots = int(round(timebytes/0.00002363))
slots = int(round(timebytes/0.00003472))
print "slots", slots
ackslots = 7
conssamples = samples-slots-ackslots
print "considered samples", conssamples

# TODO slide over samples
good = 0
goodack = 0
bad = 0
for i in range (1,samples-(slots+ackslots)):
    # check if datapacket makes it
    if (int(content[i]) == 0):
        idle = 1
        for j in range(i,i+slots):
            if (int(content[j]) == 1):
                idle = 0
                break
        if (idle == 1):
            good = good + 1
            # if datapacket has made it, check if ack also makes it
            idle = 1
            for j in range(i,i+(slots+ackslots)):
                if (int(content[j]) == 1):
                    idle = 0
                    break
            if (idle == 1):
                goodack = goodack + 1
        else:
            bad=bad+1
    else:
        bad = bad + 1
print "good",good
print "goodack",goodack
print "bad",bad
prr = float(good)/float(conssamples)
print "prr",prr
dep = float(goodack)/float(good)
print "dep",dep
