// SPDX-License-Identifier: MIT OR Apache-2.0
#include <errno.h>
#include <stddef.h>
/* Deliberately permits a successful result with unusable output/capacity,
 * so the wrapper's observation guards are tested independently. */
int convert_fromBigEndian_toMachine_ng(void *descriptor,void *input,unsigned size,
                                      void *output,unsigned version,unsigned capacity){
 int result=*(const int *)descriptor;
 if(errno!=EDOM||version!=0x22038u){errno=EINVAL;return -99;}
 unsigned n=size<capacity?size:capacity;
 if(output&&input)for(unsigned i=0;i<n;i++)
  ((unsigned char *)output)[i]=((unsigned char *)input)[i]^0x5a;
 errno=ERANGE;return result;
}
