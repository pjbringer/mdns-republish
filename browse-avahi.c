#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include <error.h>
#include <client.h>
#include <lookup.h>
#include <simple-watch.h>
#include <defs.h>

#include "browse.h"

#define GCC_UNUSED __attribute__((unused))

struct DiscoveryData {
	AddAddressCallback add_address_callback;
	RemoveAddressCallback remove_address_callback;
	AvahiProtocol proto;
	AvahiClient *client;
	AvahiServiceBrowser *browser;
	void *clientdata;
};

void avahi_add_address(const char *name, const AvahiAddress *a, AddAddressCallback cb, void *clientdata) {
	unsigned short sa_family = a->proto == AVAHI_PROTO_INET ? AF_INET : AF_INET6;
	cb(name, sa_family, a->data.data, clientdata);
}

void avahi_remove_address(const char *name, RemoveAddressCallback cb, void *clientdata) {
	cb(name, clientdata);
}

static void avahi_service_resolver_callback(
	AvahiServiceResolver *r GCC_UNUSED,
	AvahiIfIndex interface GCC_UNUSED,
	AvahiProtocol protocol GCC_UNUSED,
	AvahiResolverEvent event,
	const char *name GCC_UNUSED,
	const char *type GCC_UNUSED,
	const char *domain GCC_UNUSED,
	const char *hostname,
	const AvahiAddress *a,
	uint16_t port GCC_UNUSED,
	AvahiStringList *txt GCC_UNUSED,
	AvahiLookupResultFlags flags GCC_UNUSED,
	void *userdata) {

	struct DiscoveryData *user = (struct DiscoveryData*) userdata;
	switch(event) {
	case AVAHI_RESOLVER_FOUND:
		assert(user->proto == AVAHI_PROTO_UNSPEC || user->proto == a->proto);
		//avahi_address_snprint(straddr, AVAHI_ADDRESS_STR_MAX, a);
		//printf("%s (%s) in domain %s was resolved to %s:%d (%s:%d)\n", name, type, domain, hostname, port, straddr, port);
		avahi_add_address(hostname, a, user->add_address_callback, user->clientdata);
		break;
	case AVAHI_RESOLVER_FAILURE:
		if (DEBUG_LEVEL>0) fprintf(stderr, "Failure in avahi_service_resolver for %s\n", hostname);
		break;
	default:
		fprintf(stderr, "Unexepected case in avahi_service_resolver_callback: %d\n", event);
	}
}

static void avahi_service_browser_callback(
		AvahiServiceBrowser *browser GCC_UNUSED,
		AvahiIfIndex interface GCC_UNUSED,
		AvahiProtocol protocol,
		AvahiBrowserEvent event,
		const char *name,
		const char *type GCC_UNUSED,
		const char *domain,
		AvahiLookupResultFlags flags GCC_UNUSED,
		void *userdata) {

	struct DiscoveryData *user = (struct DiscoveryData*) userdata;
	switch(event) {
	case AVAHI_BROWSER_NEW:
		avahi_service_resolver_new(user->client, interface, protocol, name, type, domain, user->proto, 0, avahi_service_resolver_callback, user);
		break;
	case AVAHI_BROWSER_REMOVE:
		// Might be multiple providers for a single name. Under what conditions will this be called?
		// user->remove_address_callback(?, user->remove_address_callback, user->clientdata);
		break;
	case AVAHI_BROWSER_CACHE_EXHAUSTED:
	case AVAHI_BROWSER_ALL_FOR_NOW:
		// ignore
		break;
	case AVAHI_BROWSER_FAILURE:
		fprintf(stderr, "Avahi browse failure\n");
		exit(-1);
	default:
		fprintf(stderr, "Unexpected case in avahi_service_browser_callback: %d\n", event);
	}
}

static void avahi_client_callback(AvahiClient *client, AvahiClientState state, void *userdata) {
	struct DiscoveryData *user = (struct DiscoveryData*) userdata;
	switch(state) {
	case AVAHI_CLIENT_S_RUNNING:
		user->browser = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_ssh._tcp", NULL, 0, avahi_service_browser_callback, userdata);
		if (!user->browser) {
			fprintf(stderr, "Could not create browser\n");
			exit(-1);
		}
		break;
	case AVAHI_CLIENT_S_REGISTERING:
	case AVAHI_CLIENT_S_COLLISION:
	case AVAHI_CLIENT_CONNECTING:
		//ignore
		break;
	case AVAHI_CLIENT_FAILURE:
		fprintf(stderr, "Client failure\n");
		break;
	default:
		fprintf(stderr, "Unexpected case in avahi_client_callback: %d\n", state);
	}
}

#if 0
void add_addr(const char *hostname, unsigned short sa_family, const unsigned char *data) {
	char straddr[50];
	inet_ntop(sa_family, data, straddr, 50);
	printf("Adding %s -> %s\n", hostname, straddr);
}

void remove_addr(const char *hostname) {
	printf("Removing %s\n", hostname);
}
#endif

static AvahiProtocol AvahiProtoFromSpec(const char *addr_spec) {
	if (!addr_spec) return AVAHI_PROTO_UNSPEC;
	if (strstr(addr_spec, "4")) {
		return strstr(addr_spec, "6") ? AVAHI_PROTO_UNSPEC : AVAHI_PROTO_INET;
	} else {
		return strstr(addr_spec, "6") ? AVAHI_PROTO_INET6 : AVAHI_PROTO_UNSPEC;
	}
}

void find_machines(const char *addr_spec, AddAddressCallback add_addr, RemoveAddressCallback remove_addr, void *clientdata) {
	int error;

	struct DiscoveryData userdata = {0};
	int error = 0;

	AvahiSimplePoll *simple_poll = avahi_simple_poll_new();
	if (!simple_poll) {
		fprintf(stderr, "Could not create simple poll");
		return;
	}

	const AvahiPoll *poll = avahi_simple_poll_get(simple_poll);
	if (!poll) {
		fprintf(stderr, "Could not create poll");
		goto cleanup_simple_poll;
	}
	AvahiClient *client = avahi_client_new(poll, 0, avahi_client_callback, &userdata, &error);
	if (error) {
		fprintf(stderr, avahi_strerror(error));
		goto cleanup_client;
	}
	userdata.client = client;
	userdata.proto = AvahiProtoFromSpec(addr_spec);
	userdata.add_address_callback = add_addr;
	userdata.remove_address_callback = remove_addr;
	userdata.clientdata = clientdata;

	avahi_simple_poll_loop(simple_poll);

	if (userdata.browser)
		avahi_service_browser_free(userdata.browser);
cleanup_simple_poll:
cleanup_client:
	if (client) avahi_client_free(client);
	avahi_simple_poll_free(simple_poll);
}

