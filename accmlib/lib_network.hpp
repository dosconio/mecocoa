#ifndef _ACCM_LIB_NETWORK_HPP
#define _ACCM_LIB_NETWORK_HPP

struct in_addr;
struct sockaddr_in;
struct timeval;

#if defined(_INC_CPP) || defined(__cplusplus)
#include <cpp/System/Network/Layer/Application/DNS.hpp>
#endif

#if defined(_INC_CPP) || defined(__cplusplus)
extern "C" {
#endif

int mcca_net_resolve_ipv4(const char* host, struct in_addr* output);
const char* mcca_net_dns_status();
int mcca_net_dns_status_code();
int mcca_net_dns_status_text_code(const char* status);
stduint mcca_net_dns_ttl();
stduint mcca_net_dns_answer_count();
stduint mcca_net_dns_address_count();
int mcca_net_dns_address(stduint index, struct in_addr* output);
stduint mcca_net_dns_cname_count();
stduint mcca_net_dns_non_a_count();
stduint mcca_net_dns_cache_count();
int mcca_net_dns_cache_entry(stduint index, const char** host, struct in_addr* output, stduint* ttl);
const char* mcca_net_dns_cache_entry_status(stduint index);
stduint mcca_net_dns_cache_entry_address_count(stduint index);
int mcca_net_dns_cache_entry_address(stduint index, stduint address_index, struct in_addr* output);
int mcca_net_dns_cache_clear();
const char* mcca_net_dns_cache_host();
stduint mcca_net_dns_cache_ttl();
int mcca_net_dns_cache_address(struct in_addr* output);
int mcca_net_dns_probe_aaaa(const char* host, stduint* ttl);
int mcca_net_dns_set_server_ipv4(const struct in_addr* address);
int mcca_net_dhcp_renew();
int mcca_net_dhcp_release();
const char* mcca_net_socket_error_name(int error);
int mcca_net_get_socket_error(int fd);
int mcca_net_get_socket_option_int(int fd, int option_name, int* output);
stduint mcca_net_timeval_milliseconds(const struct timeval* timeout);
int mcca_net_parse_ipv4(const char* text, uint8 output[4]);
int mcca_net_parse_ipv4_host(const char* text, uint32* output);
int mcca_net_parse_port(const char* text, uint16* output);
int mcca_net_format_ipv4(char* output, stduint capacity, const uint8 address[4]);
int mcca_net_format_sockaddr_ipv4(char* output, stduint capacity, const struct sockaddr_in* address);
int mcca_net_http_build_get(char* output, stduint capacity, const char* host, const char* path);
int mcca_net_http_status_code(const char* response, stduint length);
int mcca_net_http_build_get_request(char* output, stduint capacity, const char* host, const char* path);
int mcca_net_http_parse_status_code(const char* response, stduint length);
stduint mcca_net_http_header_length(const char* response, stduint length);
int mcca_net_http_content_length(const char* response, stduint length, stduint* output);

#if defined(_INC_CPP) || defined(__cplusplus)
}

int mcca_net_resolve_ipv4_result(const char* host, uni::Network::DNSIPv4Result& result);

#endif

#endif
