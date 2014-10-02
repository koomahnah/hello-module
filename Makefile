obj-m += inverter_core.o
obj-m += list.o
obj-m += inverter.o
inverter-objs := inverter_core.o list.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:

	make -C /lib/modules/`uname -r`/build M=`pwd` clean
