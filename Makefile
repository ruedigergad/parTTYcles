COMPILER = gcc

CFLAGS := -lm -march=native -Wall -Wextra -Wdouble-promotion -Wpedantic -Wstrict-prototypes -Wshadow -O3

parttycles:
	$(COMPILER) parttycles.c MQTT-C/src/*.c json-parser/json.c TermGL/src/*.c -o parttycles -I MQTT-C/include -I json-parser -lpthread $(CFLAGS)
clean:
	rm -rf parttycles
