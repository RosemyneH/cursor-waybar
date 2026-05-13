PREFIX ?= $(HOME)/.local
CFLAGS += -std=c11 -O2 -Wall -Wextra -Iinclude -Ithird_party
LDFLAGS += -lcurl -lm

TARGET := cursor-waybar-usage
OBJS := src/main.o src/arena.o src/http.o src/token.o src/cursor_api.o \
	src/billing.o src/period_api.o third_party/cJSON.o

FALLBACK_PNG := assets/cursor-icon-fallback.png
WAYBAR_JSON := waybar/cursor-usage.jsonc waybar/cursor-brand.jsonc

.PHONY: all clean install install-setup install-fallback install-cursor-icon install-waybar-json

all: $(TARGET) $(FALLBACK_PNG)

$(FALLBACK_PNG): scripts/gen_fallback_icon.py
	python3 scripts/gen_fallback_icon.py

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c include/cursor_waybar.h include/cw_arena.h
	$(CC) $(CFLAGS) -c $< -o $@

third_party/cJSON.o: third_party/cJSON.c third_party/cJSON.h
	$(CC) -std=c11 -O2 -Wall -Ithird_party -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET) install-setup install-fallback install-cursor-icon install-waybar-json
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m755 $(TARGET) "$(DESTDIR)$(PREFIX)/bin/"

install-waybar-json: $(WAYBAR_JSON)
	install -d "$(DESTDIR)$(PREFIX)/share/cursor-waybar/waybar"
	install -m644 $(WAYBAR_JSON) "$(DESTDIR)$(PREFIX)/share/cursor-waybar/waybar/"

install-cursor-icon:
	@if [ -f assets/cursor-icon.png ]; then \
		install -d "$(DESTDIR)$(PREFIX)/share/cursor-waybar"; \
		install -m644 assets/cursor-icon.png \
			"$(DESTDIR)$(PREFIX)/share/cursor-waybar/cursor-icon.png"; \
	fi

install-fallback: $(FALLBACK_PNG)
	install -d "$(DESTDIR)$(PREFIX)/share/cursor-waybar"
	install -m644 $(FALLBACK_PNG) \
		"$(DESTDIR)$(PREFIX)/share/cursor-waybar/cursor-icon-fallback.png"

install-setup:
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m755 scripts/cursor-waybar-setup "$(DESTDIR)$(PREFIX)/bin/cursor-waybar-setup"
	install -m755 scripts/cursor-waybar-icon "$(DESTDIR)$(PREFIX)/bin/cursor-waybar-icon"
