COMPILER = gcc

CFLAGS := -lm -march=native -Wall -Wextra -Wdouble-promotion -Wpedantic -Wstrict-prototypes -Wshadow -O3

parTTYcles:
	$(COMPILER) parTTYcles.c MQTT-C/src/*.c json-parser/json.c TermGL/src/*.c -o parTTYcles -I MQTT-C/include -I json-parser -lpthread $(CFLAGS)
clean:
	rm -rf parTTYcles
