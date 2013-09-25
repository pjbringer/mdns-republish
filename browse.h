#ifndef BROWSE_H
#define BROWSE_H

extern int DEBUG_LEVEL;

typedef void (*AddAddressCallback)(const char* name, unsigned short sa_family, const unsigned char* ip_addr_data, void *userdata);
typedef void (*RemoveAddressCallback)(const char* name, void *userdata);

/*
 * Find machines using ssh server advertisements. Maybe we can convert to any service at a later point.
 */
void find_machines(const char *addr_spec, AddAddressCallback add_addr, RemoveAddressCallback remove_addr, void *userdata);

#endif

