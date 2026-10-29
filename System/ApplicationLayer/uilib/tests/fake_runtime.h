#ifndef PUNOS_UILIB_FAKE_RUNTIME_H
#define PUNOS_UILIB_FAKE_RUNTIME_H

#include <appframework/app.h>
#include <special/ipc.h>

void fake_connect_clients(void);
u64 fake_last_request(bool compositor, IpcMessageType type);
usize fake_sent_count(bool compositor, IpcMessageType type);
bool fake_last_had_descriptor(bool compositor, IpcMessageType type);
void fake_deliver(bool compositor, const IpcMessage *message);

#endif
