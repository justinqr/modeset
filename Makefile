# This is the common makerule for reference
#

TARGET = modeset
OBJ = modeset.o


INCLUDES += -I$(SDKTARGETSYSROOT)/usr/include
INCLUDES += -I$(SDKTARGETSYSROOT)/usr/include/libdrm


# CFLAGS
CFLAGS += -DLINUX -Wall -c

# LDFLAGS
LDFLAGS += -L$(SDKTARGETSYSROOT)/usr/lib
LDFLAGS +=  --sysroot=${SDKTARGETSYSROOT}


CC = $(CROSS_COMPILE)gcc
CPP = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)gcc


%.o:%.c
	$(CC) $(CFLAGS) $(INCLUDES) $(@D)/$(<F) -o $(@D)/$(@F) 

%.o:%.cpp
	$(CPP) $(CFLAGS) $(INCLUDES) $(@D)/$(<F) -o $(@D)/$(@F)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)  -ldrm -lpthread
	
.PHONY: clean

clean:
	- rm -f $(TARGET) $(OBJ)
