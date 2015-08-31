all:
	clang -g -o veil veil.c -framework CoreServices
clean:
	rm -f ./veil *.o
