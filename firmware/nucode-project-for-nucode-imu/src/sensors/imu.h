#pragma once
#include "config.h"
#if CFG_IMU_MODEL == 2
#include "imu_lsm6ds3.h"
#else
#include "imu_mpu6050.h"
#endif
