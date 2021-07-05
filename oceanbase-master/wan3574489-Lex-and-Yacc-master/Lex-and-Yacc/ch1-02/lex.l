%{
 #include <stdio.h>
%}


%%

[\t ]+		;

is |
am |
are |
has |
have |
had |
go  		{ printf("%s:is a verb\n",yytext); }

very |
simply |
angrily 	{ printf("%s:is  a adverb\n",yytext); }

to |
from |
below		{ printf("%s:is  a preposition\n",yytext); }

if |
then |
and |
but |
or		{ printf("%s:is  a conjunction\n",yytext); }

my |
your |
his |
its 		{ printf("%s:is  a adjective\n",yytext); }

I |
you |
he |
they		{ printf("%s:is  a adjective\n",yytext); }


[a-zA-Z]+ 	{ printf("%s:do not recognize,might be a noun\n",yytext); }
  
.|\n 		{ ECHO; }
%%

main()
{

 yylex();


}
