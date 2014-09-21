#!/bin/sh
/sbin/insmod ./inverter.ko $* || exit 1
module="inverter"
rm -f /dev/${module}[0-1]

major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
mknod /dev/${module}0 c $major 0
mknod /dev/${module}1 c $major 1

#chgrp staff /dev/${module}[0-1]
chmod 664 /dev/${module}[0-1]
