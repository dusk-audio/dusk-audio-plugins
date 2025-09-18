# Makefile for SSL 4000 EQ LV2 Plugin with Inline Display

CC = gcc
CXX = g++
CFLAGS = -Wall -O2 -fPIC -DPIC -std=c11 -I.
LDFLAGS = -shared -lm
PKG_CONFIG = pkg-config

# Get package flags
LV2_CFLAGS =
CAIRO_CFLAGS = $(shell $(PKG_CONFIG) --cflags cairo)
CAIRO_LIBS = $(shell $(PKG_CONFIG) --libs cairo)

# Install directory
INSTALL_DIR = ~/.lv2/SSL_4000_EQ.lv2

# Source and target
SRC = ssl4keq_fixed.c
TARGET = ssl4keq.so

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LV2_CFLAGS) $(CAIRO_CFLAGS) -o $@ $< $(LDFLAGS) $(CAIRO_LIBS)

install: $(TARGET) manifest.ttl dsp.ttl
	mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/
	cp manifest.ttl $(INSTALL_DIR)/
	cp dsp.ttl $(INSTALL_DIR)/

clean:
	rm -f $(TARGET)

uninstall:
	rm -rf $(INSTALL_DIR)

test: install
	lv2info https://example.com/plugins/SSL_4000_EQ_Pure

.PHONY: all install clean uninstall test