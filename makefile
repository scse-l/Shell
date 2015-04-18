user-sh : bison.tab.o execute.o
	cc -o user-sh bison.tab.o execute.o lex.yy.o -lfl
bison.tab.o : bison.tab.c global.h
	cc -c bison.tab.c lex.yy.c
execute.o : execute.c global.h
	cc -c execute.c
bison.tab.c:bison.y req1.l
	bison -d bison.y
	flex req1.l
clean :
	rm user-sh bison.tab.o execute.o bison.tab.c lex.yy.o lex.yy.c
