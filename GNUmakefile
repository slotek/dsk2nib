include Makefile

debug: clean
debug: CFLAGS += -DDEBUG=1
debug: CFLAGS += -g
debug: CFLAGS += -fsanitize=address
debug: LDLIBS += -lasan
debug: all
