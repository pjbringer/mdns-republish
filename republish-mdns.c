#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include "browse.h"
#include "resolve.h"

#define GCC_UNUSED __attribute__((unused))

int DEBUG_LEVEL = 0;

struct BrowseData {
	char *remote_domain;
	char *server;
	char *priv_key;
	int record_ttl;
};

struct ResolveData {
	const char* hostname;
	struct BrowseData g;
	int refC;
};

/*
 * Resolves a hostname to ipv6 addresses. Return number of addresses found.
 */
int resolve_ipv6(const char* hostname, struct sockaddr_in6 *res, int res_length) {
	struct addrinfo hints = {0};
	struct addrinfo *result = NULL, *rp;
	int ret;

	hints.ai_family=AF_INET6;
	hints.ai_socktype=SOCK_STREAM;

	ret = getaddrinfo(hostname, NULL, &hints, &result);

	if (ret) {
		if (ret != EAI_NONAME)
			fprintf(stderr, "getaddrinfo: %s(%d)\n", gai_strerror(ret), ret);
		return 0;
	}

	for (rp=result, ret=0; rp && ret<res_length; rp=rp->ai_next) {
		struct sockaddr* sa = rp->ai_addr;
		if (sa->sa_family != AF_INET6) { 
			fprintf(stderr, "Assertion error, address was not IPv6\n");
			continue;
		}
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*) sa;
		res[ret++] = *sa6;

		//char straddr[INET6_ADDRSTRLEN];
		//inet_ntop(AF_INET6, sa6->sin6_addr.s6_addr, straddr, INET6_ADDRSTRLEN);
		//printf("resolve_ipv6: Address is %s\n", straddr);
	}

	if (result) {
		freeaddrinfo(result);
	}

	return ret;
}

static void popen_nsupdate(const char *priv_key, const char *input) {
	static int tolerance = 5;
	char command[512];
	snprintf(command, sizeof(command), "nsupdate -k %s", priv_key);
	FILE *nsupdate_stdin = popen(command, "we");
	if (!nsupdate_stdin) {
		fprintf(stderr, "Could not popen() nsupdate\n");
		exit(-1);
	}
	if (fputs(input, nsupdate_stdin) == EOF ||
		pclose(nsupdate_stdin)) {
		fprintf(stderr, "Unhandled error running nsupdate. Tolerating %d more failures.\n", --tolerance);
		if (!tolerance)
			exit(-1);
	}
	// Apparently server does nothing if rushed (ddos protection of some kind)
	// So we pace our-self. We're in not rush anyway
	usleep(100000);
}

static int add_ipv6(const char *hostname, const unsigned char *ip_data, int record_ttl, const char *server, const char *priv_key) {
	char input[4096];
	char straddr[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, ip_data, straddr, INET6_ADDRSTRLEN);
	if (DEBUG_LEVEL>0) printf("A %s -> %s\n", hostname, straddr);

	snprintf(input, sizeof(input), "server %s\nupdate add %s %d AAAA %s\nsend\nquit", server, hostname, record_ttl, straddr);
	popen_nsupdate(priv_key, input);

	return 0;
}

static int remove_ipv6(const char *hostname, const char *server, const char *priv_key) {
	char input[4096];

	snprintf(input, sizeof(input), "server %s\nupdate delete %s AAAA\nsend\nquit", server, hostname);
	popen_nsupdate(priv_key, input);

	return 0;
}

static void verify_ipv6_cb(unsigned short sa_family GCC_UNUSED, const void* ip_data, const char* name, void* userdata) {
	char input[4096];
	char straddr[INET6_ADDRSTRLEN];
	struct ResolveData *rd = userdata;
	struct BrowseData *g = &rd->g;
	char* pos;
	assert(rd->hostname);

	inet_ntop(AF_INET6, ip_data, straddr, INET6_ADDRSTRLEN);

	if (name==0 || !(pos=strstr(name, ".local")) || strcmp(pos, ".local") || strncmp(name, rd->hostname, pos-name)) {
		if (DEBUG_LEVEL>0) printf("D %s -> %s on server %s\n", rd->hostname, straddr, g->server);
		snprintf(input, sizeof(input), "server %s\nupdate delete %s AAAA %s\nsend\nquit", g->server, rd->hostname, straddr);
		popen_nsupdate(g->priv_key, input);
	}
	if (--(rd->refC) == 0) {
		free((void*)rd->hostname);
		free(rd);
	}
}

static char *dns_domain_replace(const char *full, const char* old_domain, const char * new_domain) {
	int llen = strlen(full) - strlen(old_domain);
	int rlen = strlen(new_domain);
	char *ret = malloc(llen + rlen + 1);
	memcpy(ret, full, llen);
	memcpy(ret+llen, new_domain, rlen);
	ret[llen+rlen] = 0;
	return ret;
}

static void add_address_callback(const char* name, unsigned short sa_family, const unsigned char* data, void* userdata, void* client_handle) {
	struct sockaddr_in6 res[10] = {{0}};
	struct BrowseData *g = userdata;
	if (sa_family != AF_INET6 || IN6_IS_ADDR_LINKLOCAL(data)) return;
	char *hostname = dns_domain_replace(name, "local", g->remote_domain);
	int already_seen = 0;

	int addr_i, addr_count = resolve_ipv6(hostname, res, 10);

	struct ResolveData* rd = malloc(sizeof(struct ResolveData));
	rd->hostname = strdup(hostname);
	rd->g = *g;
	rd->refC = 0;

	for (addr_i=0; addr_i < addr_count; addr_i++) {
		if (DEBUG_LEVEL) {
			char straddr[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, res[addr_i].sin6_addr.s6_addr, straddr, INET6_ADDRSTRLEN);
			if (DEBUG_LEVEL>1) printf("? %s -> %s (so %d)\n", hostname, straddr, memcmp(data, res[addr_i].sin6_addr.s6_addr, 16));
		}
		if (memcmp(data, res[addr_i].sin6_addr.s6_addr, 16)) {
			resolve_address(client_handle, AF_INET6, res[addr_i].sin6_addr.s6_addr, verify_ipv6_cb, rd);
			rd->refC++;
		} else
			already_seen = 1;
	}
	if (!already_seen)
		add_ipv6(hostname, data, g->record_ttl, g->server, g->priv_key);
	free(hostname);
}

static void remove_address_callback(const char *name, void *userdata, void* client_handle GCC_UNUSED) {
	struct BrowseData *g = userdata;
	char *hostname = dns_domain_replace(name, "local", g->remote_domain);

	printf("- %s\n", hostname);

	remove_ipv6(hostname, g->server, g->priv_key);
	free(hostname);
}

int main(int argc, char **argv) {
	char *remote_domain = NULL;
	char *dns_server = NULL;
	char *priv_key = NULL;
	int record_ttl = 600;

	argc--; argv++;

	while (argc>0) {
		if (strcmp(argv[0], "-h")==0) {
			printf("Usage: publish-dns -k private_key_file -d remote_domain -s dns_server [-t record_ttl]\n");
			exit(0);
		} else if (strcmp(argv[0], "-v") == 0) {
			DEBUG_LEVEL=1;
		} else if (strcmp(argv[0], "-k")==0 && argc>1) {
			priv_key = argv[1];
			argc--; argv++;
		} else if (strcmp(argv[0], "-d")==0 && argc>1) {
			remote_domain = argv[1];
			argc--; argv++;
		} else if (strcmp(argv[0], "-s")==0 && argc>1) {
			dns_server = argv[1];
			argc--; argv++;
		} else if (strcmp(argv[0], "-t")==0 && argc>1) {
			record_ttl = atoi(argv[1]);
			argc--; argv++;
		} else {
			fprintf(stderr, "Unknown option: %s\nTry -h for help.", argv[0]);
			exit(1);
		}
		argc--; argv++;
	}

	if (!priv_key || !*priv_key) {
		fprintf(stderr, "Private key unsuitable");
		exit(-1);
	}
	if (!dns_server || !*dns_server) {
		fprintf(stderr, "DNS Server unsuitable.");
		exit(-1);
	}
	if (!remote_domain || !*remote_domain) {
		fprintf(stderr, "DNS Server unsuitable.");
		exit(-1);
	}

	struct BrowseData userdata = {remote_domain, dns_server, priv_key, record_ttl};
	find_machines("6", add_address_callback, remove_address_callback, &userdata);

	return 0;
}

