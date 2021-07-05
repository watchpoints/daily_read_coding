%{
 #include <stdio.h>
%}


%%

-?(([0-9]+)|([0-9]*\.[0-9]+)([eE][-+]?[0-9]+)?) { printf("number\n"); }

\"[^"]*\" { printf("%s\n",yytext); }

%%

main()
{

 yylex();


}
