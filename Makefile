all: modules
MODULE_NAME := dtv_params

obj-m = dtv_params.o

ccflags-y += -std=gnu99
ccflags-y += -Wdeclaration-after-statement


modules: $(driver_dependencies)
	@echo build driver
	$(MAKE) V=1 ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) modules
