#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdint.h>

static inline uint16_t htons(uint16_t val) { 
    return (uint16_t)((val << 8) | (val >> 8)); 
}

static inline uint16_t ntohs(uint16_t val) { 
    return htons(val); 
}

// Utiliza builtins do compilador para garantir portabilidade e otimização
static inline uint32_t htonl(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >>  8) |
           ((val & 0x0000FF00) <<  8) |
           ((val & 0x000000FF) << 24);
#endif
}

static inline uint32_t ntohl(uint32_t val) { 
    return htonl(val); 
}

#endif // NET_UTILS_H
