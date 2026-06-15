OUTDIR = build
TARGET = $(OUTDIR)/network-daemon

CC = aarch64-linux-gnu-gcc
GLIB_DIR = third_party/glib

INCLUDES = -Iinc
SRC = src

CFLAGS = -O2

INCLUDES += -I$(GLIB_DIR)/include \
            -I$(GLIB_DIR)/include/glib-2.0 \
            -I$(GLIB_DIR)/lib/glib-2.0/include

LIBS += -L$(GLIB_DIR)/lib
LIBS += -lgio-2.0 -lgobject-2.0 -lglib-2.0

LDFLAGS = -Wl,-rpath-link=$(GLIB_DIR)/lib \
          -Wl,--allow-shlib-undefined

all: $(TARGET)

$(TARGET): $(SRC)/*.c
	mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $(TARGET) $^ $(INCLUDES) $(LIBS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

strip: $(TARGET)
	aarch64-linux-gnu-strip $(TARGET)

.PHONY: all clean strip
