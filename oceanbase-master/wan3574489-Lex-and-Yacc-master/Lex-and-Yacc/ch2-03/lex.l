%{
  unsigned charCount = 0,wordCount = 0,lineCount = 0;
  
  #undef yywrap

%}

word [^ \t\n]+
eol \n

%%

{word}	{ wordCount++;charCount += yyleng; }
{eol}   { charCount++;lineCount++; }
.    	 charCount++;

%%

char **fileList;
unsigned currentFile = 0;
unsigned nFiles;
unsigned long totalCC = 0;
unsigned long totalWC = 0;
unsigned long totalLC = 0;


main(argc,argv)
int argc;
char **argv;
{
 FILE *file;
 
 fileList = argv+1;
 nFiles   = argc-1;
 
 if(argc == 2){
   currentFile = 1;
   file = fopen(argv[1],"r");
   if(!file){
     fprintf(stderr,"could not open %s\n",argv[1]);
     exit(1);
    }
    yyin = file;
 }
 
 if(argc > 2)
    yywrap();


 yylex();
 
 if(argc > 2){
   totalCC += charCount;
   totalWC += wordCount;
   totalLC += lineCount;
   printf("Word:%d,Char:%d,line:%d\n",totalWC,totalCC,totalLC);
 }else{
   printf("Word:%d,Char:%d,line:%d\n",wordCount,charCount,lineCount);
 }
 
 
 return 0;
}


yywrap(){
  FILE *file = NULL;

  if( (currentFile != 0) && (nFiles > 1) && (currentFile < nFiles) ){
  	printf("Word:%d,Char:%d,line:%d,file:%s\n",wordCount,charCount,lineCount,fileList[currentFile-1]);
  	totalCC += charCount;
        totalWC += wordCount;
        totalLC += lineCount;
        charCount = wordCount = lineCount = 0;
        fclose(yyin);
  }

  while(fileList[currentFile] != (char *)0){
   file = fopen(fileList[currentFile++],"r");
   if(file != NULL){
    yyin = file;
    break;
   }
   fprintf(stderr,"Could not open %s\n",fileList[currentFile - 1]);
  }

  return (file?0:1);
}

















