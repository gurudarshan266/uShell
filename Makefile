all:
	gcc -g -o ush Main.cpp parse.c Job.cpp -lstdc++ -Wno-write-strings

clean:
	rm ush

tar:
	 tar -zcvf gpollep.tar.gz Main.cpp Job.cpp Job.h Makefile parse.h parse.c EXTRA_CREDIT

ush.1.ps:	ush.1
	groff -man -T ps ush.1 > ush.1.ps

