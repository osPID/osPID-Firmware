#ifndef OSPASSERT_H
#define OSPASSERT_H

// each file is expected to #define BUGCHECK to its appropriate
// block identifier and __LINE__

void ospBugCheck(const char *block, int line);

#define ospAssert(x)	\
  if (!(x))		\
    BUGCHECK()

// |name| must be a valid identifier
#define OSP_STATIC_ASSERT(condition, name)			\
  typedef char assert_failed_ ## name [ (condition) ? 1 : -1 ];

#endif

