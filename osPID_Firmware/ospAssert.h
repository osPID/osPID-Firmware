#ifndef OSPASSERT_H
#define OSPASSERT_H

// each file is expected to #define BUGCHECK to its appropriate
// block identifier and __LINE__

void ospBugCheck(const char *block, int line);

#define ospAssert(x)	\
  if (!(x))		\
    BUGCHECK()

#endif

