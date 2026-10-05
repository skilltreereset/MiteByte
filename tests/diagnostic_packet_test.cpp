#include "../fleabyte/src/tools/diagnostic_packet.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
  unsigned char packet[400] = {}; char text[184];
  for (size_t size = 0; size < 34; ++size) assert(!hotspotDiagnosticPacketSummary(packet, size, text, sizeof(text)));
  packet[12]=8; packet[14]=0x45; packet[16]=0; packet[17]=48; packet[23]=17;
  packet[26]=192; packet[27]=168; packet[28]=137; packet[29]=1;
  packet[30]=192; packet[31]=168; packet[32]=137; packet[33]=224;
  packet[34]=0; packet[35]=67; packet[36]=0; packet[37]=68; packet[38]=0; packet[39]=28;
  packet[42]=2; packet[46]=0x12; packet[47]=0x34; packet[48]=0x56; packet[49]=0x78;
  assert(hotspotDiagnosticPacketSummary(packet, 62, text, sizeof(text)));
  assert(strstr(text,"DHCP") && strstr(text,"192.168.137.1:67") && strstr(text,"id=305419896"));
  assert(!hotspotDiagnosticPacketSummary(packet, 61, text, sizeof(text))); // truncated IP
  packet[20]=0x20; // first fragment may carry protocol header
  assert(hotspotDiagnosticPacketSummary(packet,62,text,sizeof(text)));
  packet[21]=1; assert(!hotspotDiagnosticPacketSummary(packet,62,text,sizeof(text))); // later fragment
  packet[20]=packet[21]=0; packet[35]=53; packet[37]=80; packet[42]=0x12;packet[43]=0x34;packet[45]=3;
  assert(hotspotDiagnosticPacketSummary(packet,62,text,sizeof(text)));
  assert(strstr(text,"DNS") && strstr(text,"code=3"));
  packet[39]=255; assert(!hotspotDiagnosticPacketSummary(packet,62,text,sizeof(text))); // invalid UDP boundary
  packet[12]=8; packet[13]=6;packet[14]=0;packet[15]=1;packet[16]=8;packet[17]=0;packet[18]=6;packet[19]=4;
  packet[20]=0;packet[21]=1;
  assert(hotspotDiagnosticPacketSummary(packet,42,text,sizeof(text)) && strstr(text,"ARP op=1"));
  assert(!hotspotDiagnosticPacketSummary(packet,41,text,sizeof(text)));
  packet[12]=8;packet[13]=0;packet[14]=0x45;packet[16]=1;packet[17]=16;packet[18]=0;packet[20]=packet[21]=0;
  packet[35]=67;packet[37]=68;packet[38]=0;packet[39]=252;
  packet[278]=99;packet[279]=130;packet[280]=83;packet[281]=99;
  packet[282]=53;packet[283]=1;packet[284]=2;packet[285]=255;
  assert(hotspotDiagnosticPacketSummary(packet,286,text,sizeof(text)) && strstr(text,"code=2"));
  packet[284]=5;
  assert(hotspotDiagnosticPacketSummary(packet,286,text,sizeof(text)) && strstr(text,"code=5"));
  packet[283]=250;
  assert(hotspotDiagnosticPacketSummary(packet,286,text,sizeof(text)));
  // Malformed variable headers and tiny output buffers cannot overrun.
  for (unsigned ihl=0;ihl<16;++ihl) {
    packet[12]=8;packet[13]=0;packet[14]=0x40|ihl;
    char small[2]={'x','x'};hotspotDiagnosticPacketSummary(packet,62,small,1);
  }
  puts("PASS: diagnostic DHCP/DNS/ARP metadata, malformed lengths and fragment boundaries");
}
