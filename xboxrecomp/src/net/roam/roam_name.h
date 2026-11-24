/* Shared wire/UI nickname rules. Keep settings and name-tag glyphs portable. */
#ifndef MC3_ROAM_NAME_H
#define MC3_ROAM_NAME_H
#include <stddef.h>
#include <string.h>
#define MC3_NICKNAME_MAX 24
static inline int mc3_nickname_char(unsigned char c)
{
    return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') ||
           c==' ' || c=='_' || c=='-' || c=='.';
}
static inline int mc3_nickname_normalize(const char *in,char out[MC3_NICKNAME_MAX+1])
{
    if(!in)return 0;
    size_t n=strlen(in);while(n&&in[n-1]==' ')n--;
    while(n&&*in==' '){in++;n--;}
    if(!n||n>MC3_NICKNAME_MAX)return 0;
    for(size_t k=0;k<n;k++)if(!mc3_nickname_char((unsigned char)in[k]))return 0;
    memcpy(out,in,n);out[n]=0;return 1;
}
#endif
