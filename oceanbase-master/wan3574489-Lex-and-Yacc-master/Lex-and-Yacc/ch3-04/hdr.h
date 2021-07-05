#define NSYMS 20

struct symtab {
  char *name;
  double value;
  double (*funcptr)();
  struct symtab *next;
}symtab;


struct symtab *symlook();
