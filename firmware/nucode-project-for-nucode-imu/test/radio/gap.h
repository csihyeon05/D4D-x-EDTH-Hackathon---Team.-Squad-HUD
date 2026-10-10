#pragma once
#include <stdint.h>
typedef int T_APP_RESULT;
typedef int T_GAP_CAUSE;
enum { GAP_CAUSE_SUCCESS=0, APP_RESULT_SUCCESS=0, GAP_ADV_STATE_IDLE=0,
 GAP_ADV_STATE_ADVERTISING=2, GAP_PARAM_DEV_STATE=1, GAP_PARAM_ADV_DATA=2,
 GAP_PARAM_BD_ADDR=3, GAP_MSG_LE_ADV_UPDATE_PARAM=64, GAP_ADTYPE_ADV_NONCONN_IND=3 };
struct T_GAP_DEV_STATE { uint8_t gap_adv_state; };
struct Response { uint16_t cause; };
struct T_LE_CB_DATA { Response* p_le_adv_update_param_rsp; };
typedef T_APP_RESULT (*Callback)(uint8_t,void*);
T_GAP_CAUSE le_get_gap_param(int,void*);
T_GAP_CAUSE le_adv_set_param(int,uint8_t,void*);
T_GAP_CAUSE le_adv_update_param();
T_GAP_CAUSE le_adv_start();
void le_register_app_cb(Callback);
void gap_get_param(int,void*);
