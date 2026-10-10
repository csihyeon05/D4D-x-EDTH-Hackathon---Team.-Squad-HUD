#include <assert.h>
#include <limits.h>
#include <string>
#include <iostream>
#include "comm/nus_queue.h"
#include "protocol/nus_json.h"
int main() {
  NusQueue q; char out[21], line[224];
  ImuPacket imu{UINT32_MAX,INT16_MIN,INT16_MAX,0,-1,2,3};
  size_t n = nus::encode(line,sizeof(line),255,UINT32_MAX,"lsm6ds3",imu);
  assert(n > 20 && n < 224); const std::string expected(line,n);
  assert(q.put(0,line,n));
  assert(q.front(out)==20); const std::string first(out);
  // A failed send does not consume bytes, and a newer snapshot cannot alter the active line.
  assert(q.front(out)==20 && first==out);
  q.commit(20);
  assert(q.put(0,"new\n",4)); assert(q.put(0,"latest\n",7));
  assert(q.dropped()==1);
  assert(q.put(1,"vitals\n",7));
  std::string received=first;
  while ((n=q.front(out))) { assert(n<=20); received.append(out,n); q.commit(n); }
  assert(received==expected+"vitals\nlatest\n");
  assert(q.put(0,"old-partial-line-with-many-bytes\n",strlen("old-partial-line-with-many-bytes\n")));
  q.front(out); q.commit(20); q.clear(); assert(q.front(out)==0);
  assert(!q.put(5,"x\n",2)); assert(!q.put(0,"no newline",10));
  VitalsPacket v{0,INT16_MIN,0,0};
  assert(nus::encode(line,sizeof(line),1,0,v)); assert(strstr(line,"\"tc\":null"));
  assert(!nus::encode(line,8,1,0,v));
  GpsPacket gps{UINT32_MAX,INT32_MIN,INT32_MAX,INT32_MIN,UINT16_MAX,1,255};
  assert(nus::encode(line,sizeof(line),255,UINT32_MAX,gps));
  OrientationPacket o{UINT32_MAX,INT32_MIN,INT32_MAX,INT32_MIN,INT32_MAX};
  assert(nus::encode(line,sizeof(line),255,UINT32_MAX,o));
  StatusPacket s{UINT32_MAX,4,UINT16_MAX,UINT32_MAX,UINT32_MAX};
  assert(nus::encode(line,sizeof(line),255,UINT32_MAX,s));
  std::cout << "NUS framing tests passed\n";
}
