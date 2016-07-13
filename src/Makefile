OS := $(shell sh -c 'uname -s 2>/dev/null || echo not')

CFLAGS=-I../third-party/include -Wall -Wextra
CXXFLAGS=-std=c++11 -I../third-party/include -Wall -Wextra

LIBS=-lpthread -L../third-party/libs -lglog -lgflags -lyuki -lpcre
ifeq ($(OS), Linux)
	LIBS+=-lunwind
endif

ifeq ($(DEBUG), 1)
	CFLAGS+=-O0 -g -DDEBUG
	CXXFLAGS+=-O0 -g -DDEBUG
else
	CFLAGS+=-O2
	CXXFLAGS+=-O2
endif

OBJS=ae.o anet.o commands.o crc32.o md5.o zmalloc.o background.o basic_io.o \
     bin_log.o client.o cocurrent_hash_map.o configuration.o db.o hash_db.o \
     iterator.o key.o obj.o persistent.o rw_spin_lock.o serialized_io.o \
     server.o worker.o

TEST_OBJS=background-test.o bin_log-test.o circular_buffer-test.o \
          cocurrent_hash_map-test.o configuration-test.o key-test.o \
          lockfree_list-test.o lockfree_ring_buffer-test.o obj-test.o \
          rw_spin_lock-test.o sanity-test.o serialized_io-test.o

all: yukino-server all-test

yukino-server: $(OBJS) main.o
	$(CXX) $(OBJS) main.o $(LIBS) -o yukino-server

all-test: $(OBJS) $(TEST_OBJS) all-test-main.o
	$(CXX) $(OBJS) $(TEST_OBJS) all-test-main.o $(LIBS) -lgtest -o all-test

yukino-objs: $(OBJS) main.o
yukino-test-objs: $(TEST_OBJS) all-test-main.o

.PHONY: clean distclean

distclean:
	rm -r ../third-party

clean:
	rm -f $(OBJS) $(TEST_OBJS) all-test-main.o main.o yukino-server all-test
