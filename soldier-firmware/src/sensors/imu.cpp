#include "config.h"
#if CFG_IMU_MODEL == 2
#include "imu_lsm6ds3_impl.h"
#else
#include "imu_mpu6050_impl.h"
#endif
