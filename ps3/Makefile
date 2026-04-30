# PSL1GHT Makefile for the initial PS3 bootstrap.
#
# This intentionally builds only ps3/bootstrap. The full game sources in ps3/src
# are experimental and should be enabled after GX/audio/input are verified.

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT must be set to your PSL1GHT SDK path)
endif

TARGET      := ACGCPS3
BUILD       := build
SOURCES     := bootstrap
DATA        := data
INCLUDES    := include

TITLE       := Animal Crossing GC PS3
APPID       := ACGC00001
CONTENTID   := UP0001-$(APPID)_00-ACGCPS3BOOTSTRAP

CFLAGS      := -O0 -g -Wall -Wextra -DTARGET_PS3 -DACGC_PS3_BOOTSTRAP_ONLY
CXXFLAGS    := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS     :=
LIBS        :=

include $(PSL1GHT)/ppu_rules
