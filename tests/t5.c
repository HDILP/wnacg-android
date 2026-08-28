#include "html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *rf(const char*p){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(n+1);fread(b,1,n,f);b[n]=0;fclose(f);return b;}
int main(void){
  char*g=rf("tests/sample_gallery.html");
  size_t hlen=strlen(g);
  /* replicate extract_fast_img_host logic inline to inspect */
  const char*p=strstr(g,"fast_img_host");
  printf("p found=%d\n", p?1:0);
  if(p){const char*eq=strchr(p,'=');printf("eq found=%d\n",eq?1:0);
    if(eq){const char*q1=strchr(eq,'\'');const char*q2=strchr(eq,'"');
      const char*q=NULL; if(q1&&(!q2||q1<q2))q=q1; else if(q2)q=q2;
      printf("q=%d\n",q?1:0);
      if(q){const char*qe=strchr(q+1,*q);printf("qe=%d q+1 len=%d\n",qe?1:0,(int)(qe-(q+1)));}}}
  return 0;
}
