#
# Makefile for the Grand Unified Firmware Interface
#

CFLAGS_gufi_of_test.o := -DDEBUG
CFLAGS_gufi_acpi_test.o := -DDEBUG

obj-$(CONFIG_GUFI) = gufi.o

obj-$(CONFIG_GUFI_TEST_DRIVER)	+= gufi_of_test.o
obj-$(CONFIG_GUFI_TEST_DRIVER)	+= gufi_acpi_test.o
