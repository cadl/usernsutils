all: bin/ns-spawn bin/nshd bin/nsh bin/nsexec bin/socketd bin/udpd

bin/%: src/%.c | bin
	gcc -Wall -Werror -D _GNU_SOURCE -o "$@" "$<" -lutil

bin:
	mkdir bin

clean:
	rm -rf bin/*
