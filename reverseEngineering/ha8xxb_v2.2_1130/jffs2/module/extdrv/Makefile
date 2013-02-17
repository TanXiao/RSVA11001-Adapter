DRIVER_LIB_BASE=/mnt
DRIVER_BASE=/mnt/fuho_driver
DRIVER_MODULE:=tw2865.ko gpioi2c.ko higpio.ko
DRIVER_MODULE+=hiwdt.ko 74hc165.ko hi_ir.ko
DRIVER_MODULE+=rtc_ds1339.ko bq32k.ko
DRIVER_MODULE+=cpu_gpio_fp01093.ko sensor_gpio_fp01093.ko keypad_gpio_fp01093.ko
DRIVER_MODULE+=keypad_no_fpga_fp01093.ko at88sc_drv_fp01093.ko sv2806_fp01093.ko
DRIVER_MODULE+=keypad_no_fpga_fp01140.ko

all:clean
	echo "build driver module link file reference to $(DRIVER_BASE)..."
	ln -s $(DRIVER_BASE)/tw2865.ko
	ln -s $(DRIVER_BASE)/gpioi2c.ko
	ln -s $(DRIVER_BASE)/higpio.ko
	ln -s $(DRIVER_BASE)/hiwdt.ko
	ln -s $(DRIVER_BASE)/74hc165.ko
	ln -s $(DRIVER_BASE)/hi_ir.ko
	ln -s $(DRIVER_BASE)/rtc_ds1339.ko
	ln -s $(DRIVER_BASE)/bq32k.ko
	ln -s $(DRIVER_BASE)/cpu_gpio_fp01093.ko
	ln -s $(DRIVER_BASE)/sensor_gpio_fp01093.ko
	ln -s $(DRIVER_BASE)/keypad_gpio_fp01093.ko
	ln -s $(DRIVER_BASE)/keypad_no_fpga_fp01093.ko
	ln -s $(DRIVER_BASE)/at88sc_drv_fp01093.ko
	ln -s $(DRIVER_BASE)/sv2806_fp01093.ko
	ln -s $(DRIVER_BASE)/keypad_no_fpga_fp01140.ko
	echo "build driver library link file reference to $(DRIVER_LIB_BASE)..."
	ln -s $(DRIVER_LIB_BASE)/libat88sc.so ../../lib/libat88sc.so
	ln -s $(DRIVER_LIB_BASE)/libdevhal.so ../../lib/libdevhal.so
clean:
	-rm -fr $(DRIVER_MODULE) ../../lib/libat88sc.so ../../lib/libdevhal.so
	
