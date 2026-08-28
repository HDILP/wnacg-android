#include "html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *rf(const char*p){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(n+1);fread(b,1,n,f);b[n]=0;fclose(f);return b;}
int main(void){
  char*g=rf("tests/sample_gallery.html");
  fprintf(stderr,"gallery read %zu\n",strlen(g));
  char**u=NULL;int n=0;
  parse_imglist(g,&u,&n);
  fprintf(stderr,"imglist n=%d\n",n);
  for(int i=0;i<n;i++){fprintf(stderr,"  %s\n",u[i]);free(u[i]);}
  free(u);
  char*fn=filename_filter("a/b:c*?\"<>|d");
  fprintf(stderr,"fn=%s\n",fn);
  free(fn);
  return 0;
}
