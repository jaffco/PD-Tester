# Project Name
TARGET = main

# Sources
CPP_SOURCES = src/main.cpp src/tcpm_driver.cpp src/platform.cpp

# USB-PD C sources
C_SOURCES = src/FUSB302.c \
            src/usb_pd_driver.c \
            src/usb_pd_protocol.c \
            src/usb_pd_policy.c

# Library Locations
include common.mk