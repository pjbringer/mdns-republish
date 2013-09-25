#ifndef RESOLVE_H
#define RESOLVE_H

extern int DEBUG_LEVEL;

typedef void (*ResolveAddressCallback)(unsigned short sa_family, const void* ip_addr_data, const char* name, void* userdata);

void resolve_address(void* client_handle, unsigned short sa_family, void* data, ResolveAddressCallback cb, void* clientdata);


#endif
