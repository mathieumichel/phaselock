#!/bin/sh

grep RX receiver.txt | wc -l && tail -2 sender.txt
