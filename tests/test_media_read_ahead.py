"""Exercise the actual read-ahead function with real positioned file reads.
Media decoders request 64 KiB; ensure those reads hit the cache and preserve
bytes, EOF and descriptor offsets. Ordinary large reads remain uncached.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / "sdk/src/kernel/kernel_file.c").read_text()
a = s.index("static ssize_t ra_read(")
b = s.index("\nNTSTATUS __stdcall xbox_NtReadFile", a)
fn = s[a:b]
harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#define RA_SIZE (128u << 10)
#define RA_SMALL (32u << 10)
static struct { int media; int64_t last_end,start; uint32_t len; uint8_t *buf; } s_ra[1];
static unsigned s_rd_seq,s_rd_hits,reads;
static ssize_t host_read(int fd,void *p,size_t n) { reads++;return read(fd,p,n); }
''' + fn + r'''
int main(void) {
 FILE *f=tmpfile();assert(f);int fd=fileno(f);uint8_t data[200000],out[65536];
 for(unsigned i=0;i<sizeof data;i++)data[i]=(uint8_t)(i*13u+i/73u);
 assert(write(fd,data,sizeof data)==sizeof data);
 s_ra[0].media=1;s_ra[0].buf=malloc(RA_SIZE);s_ra[0].last_end=-1;
 assert(ra_read(0,fd,out,65536,0)==65536);assert(!memcmp(out,data,65536));assert(reads==1);
 assert(ra_read(0,fd,out,65536,65536)==65536);assert(!memcmp(out,data+65536,65536));assert(reads==1);
 assert(lseek(fd,0,SEEK_CUR)==131072);
 assert(ra_read(0,fd,out,1024,123)==1024);assert(!memcmp(out,data+123,1024));assert(lseek(fd,0,SEEK_CUR)==1147);
 assert(ra_read(0,fd,out,65536,190000)==10000);assert(!memcmp(out,data+190000,10000));assert(lseek(fd,0,SEEK_CUR)==200000);
 assert(ra_read(0,fd,out,65536,200000)==0);
 s_ra[0].media=0;s_ra[0].len=0;s_ra[0].last_end=0;unsigned before=reads;
 assert(ra_read(0,fd,out,65536,0)==65536);assert(ra_read(0,fd,out,65536,65536)==65536);assert(reads==before+2);
 assert(!memcmp(out,data+65536,65536));free(s_ra[0].buf);fclose(f);
 puts("media read-ahead: data, cache hits, seeks, EOF and ordinary reads passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/"test.c").write_text(harness)
 subprocess.run(["cc","-O2",str(p/"test.c"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
