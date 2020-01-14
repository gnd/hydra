# Hydra - Sony Action Cam viewer
TARGET=hydra
DEBUG=no
CC=gcc

CFLAGS =-Wall
CFLAGS+=-Wextra
CFLAGS+=-Wwrite-strings
CFLAGS+=-Wpointer-arith
CFLAGS+=-Wfloat-equal
CFLAGS+=-Wcast-align
ifeq (yes, $(DEBUG))
  CFLAGS+=-g
else
  CFLAGS+=-O2 -s
endif
#CFLAGS += -DPLATFORM_LINUX $(shell pkg-config --cflags glfw3 glu gl)

LIBS+=-lglfw
LIBS+=-lGL
LIBS+=-lX11
LIBS+=-lXrandr
LIBS+=-lXi
LIBS+=-lm
LIBS+=-lpthread
LIBS+=-ldl
LIBS+=-lcurl
LIBS+=-ljpeg

SOURCES=hydra.c

OBJECTS=$(subst .c,.o, $(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

clean:
	rm -f *~
	rm -f $(OBJECTS) $(TARGET)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $<
