#define NAME 257
#define NUMBER 258
#define UMINUS 259
typedef union {
  double dval;
  struct symtab *symp;
} YYSTYPE;
extern YYSTYPE yylval;
