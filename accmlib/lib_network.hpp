#ifndef _ACCM_LIB_NETWORK_HPP
#define _ACCM_LIB_NETWORK_HPP

struct in_addr;
struct sockaddr_in;

#if defined(_INC_CPP) || defined(__cplusplus)
extern "C" {
#endif

int mcca_net_resolve_ipv4(const char* host, struct in_addr* output);
const char* mcca_net_dns_status();
stduint mcca_net_dns_ttl();
stduint mcca_net_dns_answer_count();
const char* mcca_net_socket_error_name(int error);
int mcca_net_get_socket_error(int fd);
int mcca_net_format_ipv4(char* output, stduint capacity, const uint8 address[4]);
int mcca_net_format_sockaddr_ipv4(char* output, stduint capacity, const struct sockaddr_in* address);

#if defined(_INC_CPP) || defined(__cplusplus)
}
#endif

#endif
