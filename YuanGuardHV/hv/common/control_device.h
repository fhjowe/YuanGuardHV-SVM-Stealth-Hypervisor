#ifndef YGHV_CONTROL_DEVICE_H
#define YGHV_CONTROL_DEVICE_H

#include <ntddk.h>

NTSTATUS yghv_control_device_init(PDRIVER_OBJECT driver);
void yghv_control_device_cleanup(PDRIVER_OBJECT driver);

#endif
