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
stduint mcca_net_dns_cname_count();
stduint mcca_net_dns_non_a_count();
stduint mcca_net_dns_cache_count();
int mcca_net_dns_cache_entry(stduint index, const char** host, struct in_addr* output, stduint* ttl);
const char* mcca_net_dns_cache_host();
stduint mcca_net_dns_cache_ttl();
int mcca_net_dns_cache_address(struct in_addr* output);
const char* mcca_net_socket_error_name(int error);
int mcca_net_get_socket_error(int fd);
int mcca_net_parse_ipv4(const char* text, uint8 output[4]);
int mcca_net_parse_ipv4_host(const char* text, uint32* output);
int mcca_net_parse_port(const char* text, uint16* output);
int mcca_net_format_ipv4(char* output, stduint capacity, const uint8 address[4]);
int mcca_net_format_sockaddr_ipv4(char* output, stduint capacity, const struct sockaddr_in* address);

#if defined(_INC_CPP) || defined(__cplusplus)
}
#endif

#endif
