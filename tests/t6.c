#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *rf(const char*p){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*b=malloc(n+1);fread(b,1,n,f);b[n]=0;fclose(f);return b;}
int main(void){
  char*g=rf("tests/sample_gallery.html");
  printf("g len=%zu\n", strlen(g));
  const char*line=strstr(g,"var imglist = ");
  printf("line offset=%ld\n", (long)(line-g));
  const char*start=strchr(line,'[');
  printf("start offset=%ld\n",(long)(start-g));
  const char*end=NULL; for(const char*p=line;*p;p++) if(*p==']') end=p;
  printf("end offset=%ld\n",(long)(end-g));
  size_t jl=(size_t)(end-start+1);
  printf("jl=%zu\n",jl);
  char*json=malloc(jl+1); memcpy(json,start,jl); json[jl]=0;
  printf("json first 30 bytes: "); for(int i=0;i<30 && i<(int)jl;i++) printf("%02x ",(unsigned char)json[i]); printf("\n");
  printf("json as string (first 60): %.*s\n", 60, json);
  return 0;
}
