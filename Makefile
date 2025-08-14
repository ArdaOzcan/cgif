test: test.c
	mkdir -p build
	clang -g gif.c test.c hashmap.c munit/munit.c -I munit/include -o build/test

main: main.c
	mkdir -p build
	clang -g main.c gif.c hashmap.c munit/munit.c -I munit/include -o build/main
