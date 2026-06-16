OUTDIR = build
TARGET = $(OUTDIR)/network-daemon

CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
GLIB_DIR = third_party/glib
SQLITE_DIR = third_party/sqlite

INCLUDES = -Iinc
SRCS = src/assets.c \
       src/config.c \
       src/db.c \
       src/http.c \
       src/main.c \
       src/notify_client.c \
       src/sms.c \
       src/util.c
ASSET_DIR = web

ASSETS = $(ASSET_DIR)/index.html \
         $(ASSET_DIR)/styles.css \
         $(ASSET_DIR)/app.js \
         $(ASSET_DIR)/vendor/layui.js \
         $(ASSET_DIR)/vendor/layui.css

ASSET_OBJS = $(patsubst $(ASSET_DIR)/%,$(OUTDIR)/assets/%.o,$(ASSETS))

CFLAGS = -O2

INCLUDES += -I$(GLIB_DIR)/include \
            -I$(GLIB_DIR)/include/glib-2.0 \
            -I$(GLIB_DIR)/lib/glib-2.0/include \
            -I$(SQLITE_DIR)/include

LIBS += -L$(GLIB_DIR)/lib
LIBS += -L$(SQLITE_DIR)/lib
LIBS += -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lsqlite3

LDFLAGS = -Wl,-rpath-link=$(GLIB_DIR)/lib \
          -Wl,-rpath-link=$(SQLITE_DIR)/lib \
          -Wl,--allow-shlib-undefined \
          -s

all: $(TARGET)

$(TARGET): $(SRCS) $(ASSET_OBJS)
	mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $(TARGET) $^ $(INCLUDES) $(LIBS) $(LDFLAGS)

$(OUTDIR)/assets/%.o: $(ASSET_DIR)/%
	mkdir -p $(dir $@)
	$(LD) -r -b binary -o $@ $<

clean:
	rm -rf $(OUTDIR)

strip: $(TARGET)
	aarch64-linux-gnu-strip $(TARGET)

.PHONY: all clean strip
