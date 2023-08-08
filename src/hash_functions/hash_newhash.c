#include <stdint.h>
#include <stddef.h>

#define mix(a,b,c) \
{ \
  a=a-b;  a=a-c;  a=a^(c>>13); \
  b=b-c;  b=b-a;  b=b^(a<<8);  \
  c=c-a;  c=c-b;  c=c^(b>>13); \
  a=a-b;  a=a-c;  a=a^(c>>12); \
  b=b-c;  b=b-a;  b=b^(a<<16); \
  c=c-a;  c=c-b;  c=c^(b>>5);  \
  a=a-b;  a=a-c;  a=a^(c>>3);  \
  b=b-c;  b=b-a;  b=b^(a<<10); \
  c=c-a;  c=c-b;  c=c^(b>>15); \
}

/* The whole new hash function */
void
newhash(const void *s, const size_t len, void *r)
{
   register uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0;
   register uint32_t *t = (uint32_t*)s;
   size_t sz;    /* how many key bytes still need mixing */

   for (sz = len; sz >= 12; sz -= 12)
   {
      a += *t++;
      b += *t++;
      c += *t++;
      mix(a, b, c);
   }
   c += len;

   unsigned char *k = (unsigned char*)t;
   switch(sz)
   {
   case 11: c=c+((uint32_t)k[10]<<24);
   case 10: c=c+((uint32_t)k[9]<<16);
   case 9 : c=c+((uint32_t)k[8]<<8);
      /* the first byte of c is reserved for the length */
   case 8 : b=b+((uint32_t)k[7]<<24);
   case 7 : b=b+((uint32_t)k[6]<<16);
   case 6 : b=b+((uint32_t)k[5]<<8);
   case 5 : b=b+k[4];
   case 4 : a=a+((uint32_t)k[3]<<24);
   case 3 : a=a+((uint32_t)k[2]<<16);
   case 2 : a=a+((uint32_t)k[1]<<8);
   case 1 : a=a+k[0];
   }
   mix(a, b, c);
   ((uint32_t*)r)[0] = c;
   ((uint32_t*)r)[1] = a;
   ((uint32_t*)r)[2] = b;
}
