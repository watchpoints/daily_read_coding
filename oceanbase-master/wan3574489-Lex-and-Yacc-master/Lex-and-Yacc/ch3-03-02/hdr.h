#define NSYMS 20

struct symtab {
  char *name;
  double value;
  struct symtab *next;
}symtab;


struct symtab *symlook();
