PREFIX ?= $(HOME)/.local
CFLAGS += -std=c11 -O2 -Wall -Wextra -Iinclude -Ithird_party
LDFLAGS += -lcurl -lm

TARGET := cursor-waybar-usage
OBJS := src/main.o src/http.o src/token.o src/cursor_api.o src/billing.o third_party/cJSON.o

.PHONY: all clean install install-setup

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c include/cursor_waybar.h
	$(CC) $(CFLAGS) -c $< -o $@

third_party/cJSON.o: third_party/cJSON.c third_party/cJSON.h
	$(CC) -std=c11 -O2 -Wall -Ithird_party -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET) install-setup
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m755 $(TARGET) "$(DESTDIR)$(PREFIX)/bin/"

install-setup:
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m755 scripts/cursor-waybar-setup "$(DESTDIR)$(PREFIX)/bin/cursor-waybar-setup"
