%{

#include "hdr.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

%}

%union {
  double dval;
  struct symtab *symp;
}
%token <symp> NAME
%token <dval> NUMBER

%left '-' '+'
%left '*' '/'
%nonassoc UMINUS

%type <dval> expression

%%
statement_list : statement '\n'
               | statement_list statement '\n'
		;

statement: NAME '=' expression	       { $1->value = $3; }
           | expression                { printf(" = %g\n ",$1); }
           ;

expression: expression '+' expression   { $$ = $1 + $3; }
        |   expression '-' expression   { $$ = $1 - $3; }
	|   expression '*' expression   { $$ = $1 * $3; }
	|   expression '/' expression   
			{
				if($3 ==0)
 				  yyerror("divide by zero");
				else
				  $$ = $1 / $3;
			}
	|   '-' expression %prec UMINUS {$$ = - $2 ;}
	|   '(' expression ')' { $$ = $2; }
        |   NUMBER		       
	|   NAME    { $$ = $1->value; }
	|   NAME '(' expression ')' {
			if($1->funcptr)
				$$ = ($1->funcptr)($3);
			else{
				printf("%s not a function \n",$1->name);
				$$ = 0.0;
			}
		}
        ;
%%






extern FILE *yyin;

main(){
 
  extern double sqrt(),exp(),log();
  
  addfunc("sqrt",sqrt);
  addfunc("exp",exp);
  addfunc("log",log); 
  
  do{
	yyparse();

  }while(!feof(yyin));
}

yyerror(s)
char *s;
{
 fprintf(stderr,"%s\n",s);
}


addfunc(name,func)
char *name;
double (*func)();
{
 struct symtab *sp = symlook(name);
 sp->funcptr = func;
}


struct symtab *
symlook(s)
char *s;
{
	char *p;
	struct symtab *sp,*tempsp;

	sp = &symtab;
        
        while(sp != NULL){
	    /* is it already here?*/
            if(sp->name && !strcmp(sp->name, s))
		return sp;
            /* is it free */
	    if(!sp->name) {
		sp->name = strdup(s);
		return sp;
	    }
            if(!sp->next){
		tempsp = (struct symtab * )malloc(sizeof(struct symtab)*1);
		tempsp->name = strdup(s);
		sp->next = tempsp;
		return tempsp;
            }else{
             sp = sp->next;
            }
            
	   
        }

} /* symlook */
