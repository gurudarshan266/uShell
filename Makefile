all:
	gcc -g -o ush Main.cpp parse.c Job.cpp -lstdc++ -Wno-write-strings

clean:
	rm ush
