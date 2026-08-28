#include "html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *rf(const char*p){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(n+1);fread(b,1,n,f);b[n]=0;fclose(f);return b;}
int main(void){
  char*sh=rf("tests/sample_search.html");
  search_result s; parse_search(sh,0,&s);
  printf("search total_page=%ld (expect 50)\n", s.total_page);
  free_search_result(&s);
  char*g=rf("tests/sample_gallery.html");
  char**u=NULL;int n=0; parse_imglist(g,&u,&n);
  for(int i=0;i<n;i++){printf("url[%d]=%s\n",i,u[i]);free(u[i]);} free(u);
  char*fn=filename_filter("a/b:c*?\"<>|d");
  printf("fn=[%s] len=%zu\n",fn,strlen(fn));
  free(fn);
  return 0;
}
