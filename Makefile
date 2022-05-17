# Copyright (c) 2022 ICE9 Consulting LLC

PROG = ice9-bluetooth
OBJS = bluetooth.o burst_catcher.o fsk.o main.o hash.o options.o help.o

EXTCAP_PATH = $(HOME)/.config/wireshark/extcap

CFLAGS = -I/opt/homebrew/include -Wall -g -O0 -fsanitize=address
LDFLAGS = -L/opt/homebrew/lib -lliquid -lbtbb -lhackrf -lpthread -fsanitize=address

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LDFLAGS)

help.o: help.c help.h
	$(CC) $(CFLAGS) -c -o help.o help.c

help.h: help.txt
	xxd -i help.txt > help.h

install:
	install -d $(EXTCAP_PATH)
	install ice9-bluetooth $(EXTCAP_PATH)

uninstall:
	rm -f $(EXTCAP_PATH)/ice9-bluetooth

clean:
	rm -f $(PROG) $(OBJS)
