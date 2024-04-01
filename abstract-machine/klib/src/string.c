#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s) {
  const char *t = s;
  while (*t != '\0') {
    ++t;
  };
  return t - s;
}

char *strcpy(char *dst, const char *src) {
  memcpy(dst, src, strlen(src) + 1);
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  char *p = dst;
  const char *q = src;
  while (n-- && *q != '\0') {
    *p++ = *q++;
  }
  return dst;
}

char *strcat(char *dst, const char *src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 == *s2) {
    if (*s1 == '\0') {
      return 0;
    }
    ++s1;
    ++s2;
  }
  return *s1 - *s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n > 0) {
    if (*s1 != *s2 || *s1 == '\0') {
      return *s1 - *s2;
    }
    ++s1;
    ++s2;
    --n;
  }
  return 0;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = s;
  while (n--) {
    *p++ = (uint8_t)c;
  }
  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *tmp = malloc(n);
  memcpy(tmp, src, n);
  memcpy(dst, tmp, n);
  free(tmp);
  return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
  uint8_t *p = out;
  const uint8_t *q = in;
  while (n--) {
    *p++ = *q++;
  }
  return out;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = s1;
  const uint8_t *p2 = s2;
  while (n > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    ++p1;
    ++p2;
    --n;
  }
  return 0;
}

#endif
