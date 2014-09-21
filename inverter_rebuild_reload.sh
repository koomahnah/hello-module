#!/bin/bash
make all && (sudo ./inverter_unload.sh 2> /dev/null; sudo ./inverter_load.sh)
