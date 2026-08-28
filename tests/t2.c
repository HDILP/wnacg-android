#include "html.h"
#include <stdio.h>
#include <stdlib.h>
static char *rf(const char*p){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(n+1);fread(b,1,n,f);b[n]=0;fclose(f);return b;}
int main(void){
  char*h=rf("tests/sample_search.html");
  fprintf(stderr,"read %zu\n",strlen(h));
  search_result s;
  fprintf(stderr,"calling parse_search\n");
  parse_search(h,0,&s);
  fprintf(stderr,"count=%d total=%ld\n",s.count,s.total_page);
  return 0;
}
