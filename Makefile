CC = clang
CFLAGS = -g -Wall

BUILD_DIR = build
SRC_DIR = .

LIB_FILES = $(SRC_DIR)/mem.c $(SRC_DIR)/gif.c $(SRC_DIR)/hashmap.c
MAIN_FILE = $(SRC_DIR)/main.c
TEST_FILE = $(SRC_DIR)/test.c
MUNIT_FILE = $(SRC_DIR)/munit/munit.c

LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_FILES))
MAIN_OBJECT = $(BUILD_DIR)/main.o
TEST_OBJECT = $(BUILD_DIR)/test.o
MUNIT_OBJECT = $(BUILD_DIR)/munit.o

all: main test

main: $(LIB_OBJECTS) $(MAIN_OBJECT)
	$(CC) $(CFLAGS) $(LIB_OBJECTS) $(MAIN_OBJECT) -o $(BUILD_DIR)/main

test: $(LIB_OBJECTS) $(TEST_OBJECT) $(MUNIT_OBJECT)
	$(CC) $(CFLAGS) $(LIB_OBJECTS) $(TEST_OBJECT) $(MUNIT_OBJECT) -o $(BUILD_DIR)/test -I $(SRC_DIR)/munit/include

$(BUILD_DIR)/munit.o: munit/munit.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean

clean:
	rm -rf $(BUILD_DIR)
