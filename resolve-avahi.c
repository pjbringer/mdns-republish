#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <address.h>
#include <client.h>
#include <lookup.h>

#include "resolve.h"

#define GCC_UNUSED __attribute__((unused))

static struct ResolveAddressData {
	ResolveAddressCallback cb;
	void* clientdata;
};

void avahi_resolution(const AvahiAddress *a, const char *name, struct ResolveAddressData* rd) {
	unsigned short sa_family = a->proto == AVAHI_PROTO_INET ? AF_INET : AF_INET6;
	rd->cb(sa_family, a->data.data, name, rd->clientdata);
}

void avahi_address_resolver_callback(
	AvahiAddressResolver *r,
	AvahiIfIndex interface GCC_UNUSED,
	AvahiProtocol protocol GCC_UNUSED,
	AvahiResolverEvent event,
	const AvahiAddress *a,
	const char *name,
	AvahiLookupResultFlags flags GCC_UNUSED,
	void *userdata) {

	char straddr[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, a->data.data, straddr, INET6_ADDRSTRLEN);
	struct ResolveAddressData *rd = userdata;
	assert(rd);
	assert(rd->cb);
	switch (event) {
		case AVAHI_RESOLVER_FOUND:
			avahi_resolution(a, name, rd);
			break;
		case AVAHI_RESOLVER_FAILURE:
			{
			char addr_str[AVAHI_ADDRESS_STR_MAX];
			avahi_address_snprint(addr_str, AVAHI_ADDRESS_STR_MAX, a);
			fprintf(stderr, "Failed on resolution of %s.\n", addr_str);
			}
			break;
	}
	avahi_address_resolver_free(r);
	free(rd);
}

void resolve_address(void* client_handle, unsigned short family, void* data, ResolveAddressCallback cb, void* clientdata) {
	AvahiClient *client = client_handle;
	AvahiAddress address;
	struct ResolveAddressData *user = malloc(sizeof(user));
	user->cb = cb;
	user->clientdata = clientdata;

	address.proto = avahi_af_to_proto(family);
	memcpy(address.data.data, data, family==AF_INET ? 4 : 16);

	avahi_address_resolver_new(
		client,
		AVAHI_IF_UNSPEC,
		AVAHI_PROTO_UNSPEC,
		&address,
		0,
		avahi_address_resolver_callback,
		user
	);
}
