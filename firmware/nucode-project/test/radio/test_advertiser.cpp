#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <BLEDevice.h>
#include "comm/advertiser.h"
extern "C" {
#include "gap.h"
}
FakeBLE BLE;
Callback callback;
uint8_t state=GAP_ADV_STATE_ADVERTISING, bytes[node::ADVERT_SIZE];
int requests=0, starts=0, requestCause=0, setCause=0;
T_GAP_CAUSE le_get_gap_param(int,void* p) { static_cast<T_GAP_DEV_STATE*>(p)->gap_adv_state=state; return 0; }
T_GAP_CAUSE le_adv_set_param(int param,uint8_t n,void* p) { assert(param==GAP_PARAM_ADV_DATA); memcpy(bytes,p,n); return setCause; }
T_GAP_CAUSE le_adv_update_param() { ++requests; return requestCause; }
T_GAP_CAUSE le_adv_start() { ++starts; return 0; }
void le_register_app_cb(Callback c) { callback=c; }
void gap_get_param(int,void* p) { memset(p,0,6); }
void logMessage(const char*,...) {}
void complete(uint16_t cause=0) { Response r{cause}; T_LE_CB_DATA d{&r}; callback(GAP_MSG_LE_ADV_UPDATE_PARAM,&d); }
int main() {
 Advertiser a; a.begin(); uint8_t p[node::PACKET_SIZE]={};
 a.send(p); a.update(1000); assert(requests==1 && a.sent()==0);
 p[1]=7; a.send(p); a.update(2000); assert(requests==1); // no overlap
 a.update(4000); assert(requests==1 && a.sent()==0); // timeout is not success
 complete(); a.update(4001); assert(a.sent()==1 && requests==2 && bytes[8]==7);
 complete(1); a.update(4300); assert(a.sent()==1 && requests==3); // retry latest
 complete(); a.update(4600); assert(a.sent()==2);
 requestCause=1; a.send(p); a.update(5000); assert(a.sent()==2);
 requestCause=0; a.update(5300); complete(); a.update(5600); assert(a.sent()==3);
 state=GAP_ADV_STATE_IDLE; a.update(7000); assert(starts==1);
 a.update(7100); assert(starts==1); // restart requests are bounded
 state=GAP_ADV_STATE_ADVERTISING;
 for (uint32_t t=8000;t<18000;t+=1000) { a.send(p); a.update(t); complete(); a.update(t+1); }
 assert(a.sent()==13 && starts==1);
 puts("advertiser: live updates, callbacks, errors and timeout tests passed");
}
