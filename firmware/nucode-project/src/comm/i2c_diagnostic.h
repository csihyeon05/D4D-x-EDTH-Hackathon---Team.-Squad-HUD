#pragma once
#include <Arduino.h>
#ifdef BOARD_AMB26
void hardwareI2cDiagnostic();
#else
inline void hardwareI2cDiagnostic() {}
#endif
