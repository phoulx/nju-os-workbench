#include <am.h>
#include <klib.h>
#include <klib-macros.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char* nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr ++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr ++;
  }
  return x;
}


static bool inited = false;
static void *curr = NULL;

void *malloc(size_t size) {
  if (!inited) {
    curr = (void *)ROUNDUP(heap.start, 8);
    inited = true;
  }
  size = (size_t)ROUNDUP(size, 8);
  void *ret = curr;
  curr += size;
  assert(heap.start <= curr && curr <= heap.end);
  return ret;
}

void free(void *ptr) {
}

#endif
