#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "browse.h"

#define GCC_UNUSED __attribute__((unused))

int DEBUG_LEVEL = 0;

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

void popen_nsupdate(const char *priv_key, const char *input) {
	char command[512];
	snprintf(command, sizeof(command), "nsupdate -k %s", priv_key);
	FILE *nsupdate_stdin = popen(command, "we");
	if (!nsupdate_stdin) {
		fprintf(stderr, "Could not popen() nsupdate\n");
		exit(-1);
	}
	if (fputs(input, nsupdate_stdin) == EOF ||
		pclose(nsupdate_stdin)) {
		fprintf(stderr, "Unhandled error running nsupdate");
		exit(-1);
	}
}

static int add_ipv6(const char *hostname,  const unsigned char *ip_data, int record_ttl, const char *server, const char *priv_key) {
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

static int update_ipv6(const char *hostname,  const unsigned char *ip_data, int record_ttl, const char *server, const char *priv_key) {
	char input[4096];
	char straddr[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, ip_data, straddr, INET6_ADDRSTRLEN);
	printf("U %s -> %s\n", hostname, straddr);

	snprintf(input, sizeof(input), "server %s\nupdate delete %s AAAA\nupdate add %s %d AAAA %s\nsend\nquit", server, hostname, hostname, record_ttl, straddr);
	popen_nsupdate(priv_key, input);

	return 0;
}

struct BrowseData {
	char *remote_domain;
	char *server;
	char *priv_key;
	int record_ttl;
};

static char *dns_domain_replace(const char *full, const char* old_domain, const char * new_domain) {
	int llen = strlen(full) - strlen(old_domain);
	int rlen = strlen(new_domain);
	char *ret = malloc(llen + rlen + 1);
	memcpy(ret, full, llen);
	memcpy(ret+llen, new_domain, rlen);
	ret[llen+rlen] = 0;
	return ret;
}

static void add_address_callback(const char* name, unsigned short sa_family, const unsigned char* data, void* userdata) {
	struct BrowseData *g = userdata;
	if (sa_family != AF_INET6) return;
	struct sockaddr_in6 res[10] = {{0}};
	char *hostname = dns_domain_replace(name, "local", g->remote_domain);

	int addr_count = resolve_ipv6(hostname, res, 10);
	switch(addr_count) {
	case 0:
		add_ipv6(hostname, data, g->record_ttl, g->server, g->priv_key);
		break;
	case 1:
		if (memcmp(res[0].sin6_addr.s6_addr, data, 16))
			update_ipv6(hostname, data, g->record_ttl, g->server, g->priv_key);
		break;
	default:
		fprintf(stderr, "Too many AAAA records for %s %d.\n", hostname, addr_count);
		update_ipv6(hostname, data, g->record_ttl, g->server, g->priv_key);
	}
	free(hostname);
}

static void remove_address_callback(const char *name, void *userdata) {
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
	int record_ttl = 7200;

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

