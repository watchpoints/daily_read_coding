#LEX = lex
#YACC = yacc

CC = gcc -DYYDEBUG=1

all : example

example: lex.yy.o y.tab.o
	$(CC) -o example lex.yy.o  y.tab.o -ll -lm

lex.yy.o : lex.yy.c 

lex.yy.o y.tab.o :hdr.h y.tab.c 
	$(CC) -c lex.yy.c y.tab.c 

y.tab.h y.tab.c  :yacc.y
	$(YACC) -d yacc.y

lex.yy.c : lex.l
	$(LEX) lex.l
