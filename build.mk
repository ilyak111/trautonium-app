# Траутониум для тачпада MacBook — сборка
#
#   make            — собрать программу (build/trautonium_touch)
#   make run        — собрать и запустить из Терминала
#   make app        — собрать build/Trautonium.app (запуск двойным щелчком)
#   make install    — собрать .app и положить в «Программы»
#   make doctor     — проверить, всё ли установлено
#   make original   — исходная версия original/trautonium_line_control.c (нужен: brew install sdl2_ttf)
#   make clean      — удалить папку build
#
# Makefile в корне содержит одну строку: include build.mk

CC      = clang
BUILD   = build
OBJDIR  = $(BUILD)/obj
TARGET  = $(BUILD)/trautonium_touch
APP     = $(BUILD)/Trautonium.app

# Homebrew на Apple Silicon живёт в /opt/homebrew, на Intel — в /usr/local
BREW := $(shell brew --prefix 2>/dev/null)
ifeq ($(BREW),)
  BREW := $(shell [ -d /opt/homebrew ] && echo /opt/homebrew || echo /usr/local)
endif

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(SDL_CFLAGS),)
  SDL_CFLAGS := -I$(BREW)/include/SDL2 -I$(BREW)/include -D_THREAD_SAFE
  SDL_LIBS   := -L$(BREW)/lib -lSDL2
endif

CFLAGS     = -Wall -Wextra -O2 -Isrc -Isrc/macos $(SDL_CFLAGS)
OBJCFLAGS  = $(CFLAGS) -fobjc-arc
FRAMEWORKS = -framework Cocoa -framework CoreText -framework ApplicationServices
LIBS       = $(SDL_LIBS) -lm $(FRAMEWORKS)

SRC_C = src/app.c src/synth.c src/presets.c src/ui.c src/recorder.c
SRC_M = src/macos/mt_input.m src/macos/platform_mac.m
HDR   = $(wildcard src/*.h src/macos/*.h)
OBJ   = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC_C)) $(patsubst src/macos/%.m,$(OBJDIR)/macos/%.o,$(SRC_M))

.PHONY: all run app install original doctor clean deps

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LIBS)
	@echo
	@echo "Готово. Запуск: make run. Приложение с иконкой: make app"

$(OBJDIR)/%.o: src/%.c $(HDR) | deps
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/macos/%.o: src/macos/%.m $(HDR) | deps
	@mkdir -p $(dir $@)
	$(CC) $(OBJCFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# ---- Trautonium.app: программа + иконка + библиотеки SDL внутри ----
# sdl2 из Homebrew — это sdl2-compat, который во время работы подгружает SDL3,
# поэтому кладём внутрь обе библиотеки и подписываем всё локальной подписью.
app: $(TARGET) resources/Info.plist resources/Trautonium.icns
	@rm -rf "$(APP)"
	@mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources" "$(APP)/Contents/Frameworks"
	@cp $(TARGET) "$(APP)/Contents/MacOS/Trautonium"
	@cp resources/Info.plist "$(APP)/Contents/Info.plist"
	@cp resources/Trautonium.icns "$(APP)/Contents/Resources/Trautonium.icns"
	@set -e; \
	exe="$(APP)/Contents/MacOS/Trautonium"; fw="$(APP)/Contents/Frameworks"; \
	ref=$$(otool -L "$$exe" | awk '/libSDL2/ {print $$1; exit}'); \
	if [ -z "$$ref" ]; then echo "Не нашёл libSDL2 в зависимостях программы"; exit 1; fi; \
	src=$$(echo "$$ref" | sed "s|@rpath|$(BREW)/lib|"); \
	cp -L "$$src" "$$fw/libSDL2-2.0.0.dylib"; chmod u+w "$$fw/libSDL2-2.0.0.dylib"; \
	install_name_tool -id @executable_path/../Frameworks/libSDL2-2.0.0.dylib "$$fw/libSDL2-2.0.0.dylib" 2>/dev/null; \
	install_name_tool -change "$$ref" @executable_path/../Frameworks/libSDL2-2.0.0.dylib "$$exe"; \
	sdl3=""; for p in "$(BREW)/opt/sdl3/lib/libSDL3.0.dylib" "$(BREW)/lib/libSDL3.0.dylib" "$(BREW)/lib/libSDL3.dylib"; do \
	  if [ -f "$$p" ]; then sdl3="$$p"; break; fi; done; \
	if [ -n "$$sdl3" ]; then \
	  cp -L "$$sdl3" "$$fw/libSDL3.dylib"; chmod u+w "$$fw/libSDL3.dylib"; \
	  install_name_tool -id @loader_path/libSDL3.dylib "$$fw/libSDL3.dylib" 2>/dev/null; \
	else echo "Внимание: libSDL3 не найдена — приложение будет работать только на этом Mac"; fi; \
	for l in "$$fw"/*.dylib; do \
	  if otool -L "$$l" | tail -n +2 | grep -q "$(BREW)"; then \
	    echo "Внимание: $$l ссылается на Homebrew — на другом Mac может не запуститься"; fi; done; \
	codesign --force --sign - "$$fw"/*.dylib 2>/dev/null; \
	codesign --force --sign - "$(APP)"
	@echo
	@echo "Готово: $(APP) — запускайте двойным щелчком или: open $(APP)"
	@echo "Положить в «Программы»: make install"

install: app
	@if [ -w /Applications ]; then dest=/Applications; else dest="$$HOME/Applications"; mkdir -p "$$dest"; fi; \
	rm -rf "$$dest/Trautonium.app"; cp -R "$(APP)" "$$dest/"; \
	echo "Установлено в $$dest/Trautonium.app"

original: original/trautonium_line_control.c | deps
	@[ -f "$(BREW)/include/SDL2/SDL_ttf.h" ] || { echo "Для исходной версии нужен SDL2_ttf: brew install sdl2_ttf"; exit 1; }
	@mkdir -p $(BUILD)
	$(CC) -Wall -O2 $(SDL_CFLAGS) original/trautonium_line_control.c -o $(BUILD)/trautonium_original $(SDL_LIBS) -L$(BREW)/lib -lSDL2_ttf -lm
	@echo "Запуск: ./$(BUILD)/trautonium_original"

# понятная ошибка вместо стены сообщений компилятора
deps:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Нет компилятора. Выполните: xcode-select --install"; exit 1; }
	@[ -f "$(BREW)/include/SDL2/SDL.h" ] || [ -n "$(shell sdl2-config --cflags 2>/dev/null)" ] || { echo "Не найден SDL2. Выполните: brew install sdl2"; exit 1; }

doctor:
	@echo "Компилятор:   $$(command -v $(CC) || echo НЕТ — xcode-select --install)"
	@echo "Homebrew:     $$(command -v brew || echo НЕТ в PATH — см. README, шаг 2)"
	@echo "Префикс brew: $(BREW)"
	@echo "SDL2:         $$([ -f $(BREW)/include/SDL2/SDL.h ] && echo есть || echo НЕТ — brew install sdl2)"
	@echo "SDL3:         $$([ -f $(BREW)/opt/sdl3/lib/libSDL3.0.dylib ] && echo есть || echo 'нет (нужна только для make app)')"
	@echo "Настройки:    ~/Library/Application Support/Trautonium"

clean:
	rm -rf $(BUILD)
