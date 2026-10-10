#include <assert.h>
#include <math.h>
#include "node/raw_heading.h"
#include "node/motion.h"
int main() {
  node::RawHeading h;
  for (unsigned i=1;i<100;++i) assert(!h.update(i*20,1,0,0,1));
  assert(h.update(2000,1,0,0,1));
  for (unsigned i=101;i<=150;++i) h.update(i*20,1,0,0,91);
  assert(fabsf(h.headingRad()+1.5707963f)<0.001f);
  const float before=h.headingRad(); h.update(10000,1,0,0,91);
  assert(h.headingRad()==before);
  node::ActivityMeter lsm(0.000122f),mpu;
  for(unsigned i=0;i<50;++i) { lsm.add(0,0,8197); mpu.add(0,0,16384); }
  assert(lsm.roll().activity<0.1f && mpu.roll().activity==0);
}
