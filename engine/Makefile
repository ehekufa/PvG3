# ОГОРОД — движок на C. Сборка: make -C engine
# Требуется только gcc и make; все ассеты уже лежат в репозитории.

CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -I../src -I.
LDLIBS   = -lm

ENGINE_SRC = og_common.c og_vm.c og_image.c og_node.c og_runtime.c \
             og_bind.c og_scene.c og_vfs.c
GAME_ASSET_SRC = ../src/font.c

all: ogorod

ogorod: $(ENGINE_SRC) $(GAME_ASSET_SRC) og_main.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

tests: vm_test engine_test demo_test android_host_test

vm_test: tests/vm_test.c $(ENGINE_SRC) $(GAME_ASSET_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

engine_test: tests/engine_test.c $(ENGINE_SRC) $(GAME_ASSET_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

demo_test: tests/demo_test.c $(ENGINE_SRC) $(GAME_ASSET_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# Android-хост на заглушках: тот же файл, что попадает в библиотеку.
FAKE_INC = -Itests/android_fake
android_host_test: tests/android_host_test.c tests/android_fake.c \
                  android/og_android.c $(ENGINE_SRC) $(GAME_ASSET_SRC) \
                  android/og_project_data.h
	$(CC) $(CFLAGS) $(FAKE_INC) -Iandroid -DOG_HOST_TEST $^ -o $@ $(LDLIBS)

android_project_data:
	python3 ../tools/pack_project.py

test: vm_test engine_test demo_test android_host_test
	./vm_test
	./engine_test
	./demo_test
	./android_host_test

shots: ogorod
	mkdir -p shots
	./ogorod run ../projects/oborona --frames 2700 --scale 2 \
		--shot 60=shots/start.bmp \
		--shot 480=shots/planting.bmp \
		--shot 1200=shots/battle.bmp \
		--shot 2400=shots/finale.bmp

clean:
	rm -f ogorod vm_test engine_test demo_test android_host_test *.o

.PHONY: all test shots clean
