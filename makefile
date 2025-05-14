OUTDIR = build
TARGET = $(OUTDIR)/network-deamon

CC = aarch64-linux-gnu-gcc

INCLUDES = -Iinc
SRC = src

CFLAGS = -O2

INCLUDES += -I/home/yuan/Data/ufi/arm_lib/glib_aarch64_gcc7.5/include \
            -I/home/yuan/Data/ufi/arm_lib/glib_aarch64_gcc7.5/include/glib-2.0 \
            -I/home/yuan/Data/ufi/arm_lib/glib_aarch64_gcc7.5/lib/glib-2.0/include \

LIBS += -L/home/yuan/Data/ufi/arm_lib/glib_aarch64_gcc7.5/lib
LIBS += -lgio-2.0 -lglib-2.0 -lgobject-2.0 -lgmodule-2.0 -lffi

RPATH = -Wl,-rpath-link=/home/yuan/Data/ufi/arm_lib/zlib/lib

all: $(TARGET)

$(TARGET): $(SRC)/*.c
	mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $(TARGET) $^ $(INCLUDES) $(LIBS) $(RPATH)

clean:
	rm -f $(TARGET)

strip: $(TARGET)
	aarch64-linux-gnu-strip $(TARGET)

.PHONY: all clean strip
