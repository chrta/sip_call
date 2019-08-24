#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := sip-client

include $(IDF_PATH)/make/project.mk

# Does not work with stringstream :(
#  undefined reference to `_impure_ptr'
#  undefined reference to `wcsftime'
#COMPONENT_LDFLAGS += -lstdc++
