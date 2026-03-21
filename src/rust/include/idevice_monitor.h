#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int kind; // 1 = connected, 2 = disconnected
    char *udid;
} IdeviceEvent;

typedef void (*IdeviceEventCallback)(const IdeviceEvent *event);

void idevice_event_subscribe(IdeviceEventCallback cb);

#ifdef __cplusplus
}
#endif