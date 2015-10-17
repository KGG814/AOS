#ifndef _APPS_SOS_SRC_DEBUG_H_
#define _APPS_SOS_SRC_DEBUG_H_

#define SOS_DEBUG 1
#define TMP_DEBUG 1
#define RTN_ON_FAIL 0

//a little hacky
#if RTN_ON_FAIL
    #define CB_DEBUG (assert(0), 1)
#else 
    #define CB_DEBUG 1
#endif

#define eprintf(...) if (SOS_DEBUG) printf(__VA_ARGS__)


#endif /* _APPS_SOS_SRC_DEBUG_H_ */
