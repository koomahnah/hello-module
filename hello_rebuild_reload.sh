#!/bin/bash
make -C ~/linux/ M=`pwd` modules && sudo ./hello_unload.sh; sudo ./hello_load.sh
