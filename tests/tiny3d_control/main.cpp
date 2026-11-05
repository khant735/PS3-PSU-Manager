/* Independent Tiny3D control: NO PSU Manager globals, font, network, files or callbacks. */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <tiny3d.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000);
static FILE *trace;
static void mark(const char *s) {
 if (trace) { fprintf(trace,"%s\n",s); fflush(trace); fsync(fileno(trace)); }
}
int main(void) {
 mkdir("/dev_hdd0/tmp",0777);
 trace=fopen("/dev_hdd0/tmp/ps3psu_tiny3d_control.log","wb");
 mark("C00 ENTER");
 int rc=tiny3d_Init(1024*1024);
 if (rc) {mark("C01 INIT FAILED");if(trace)fclose(trace);return 1;}
 mark("C01 INIT OK");
 ioPadInit(7);
 tiny3d_Project2D();
 mark("C02 PROJECT2D");
 tiny3d_Clear(0x000000FF,TINY3D_CLEAR_ALL);
 mark("C03 CLEAR");
 tiny3d_SetPolygon(TINY3D_TRIANGLES);
 mark("C04 SETPOLYGON");
 tiny3d_VertexPos(100,100,0);tiny3d_VertexColor(0xFFFFFFFF);
 mark("C05 VERTEX1");
 tiny3d_VertexPos(200,100,0);tiny3d_VertexColor(0xFFFFFFFF);
 mark("C06 VERTEX2");
 tiny3d_VertexPos(150,200,0);tiny3d_VertexColor(0xFFFFFFFF);
 mark("C07 VERTEX3");
 tiny3d_End();
 mark("C08 END");
 tiny3d_Flip();
 mark("C09 FLIP");
 for(int i=0;i<300;i++){sysUtilCheckCallback();usleep(16667);}
 mark("C10 EXIT");
 tiny3d_Exit();
 if(trace)fclose(trace);
 return 0;
}
