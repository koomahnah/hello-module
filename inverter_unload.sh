#!/bin/sh
/sbin/rmmod inverter.ko || exit 1
rm -f /dev/inverter[0-1]
