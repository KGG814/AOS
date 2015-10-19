#ifndef _APPS_SOS_SRC_DEBUG_H_
#define _APPS_SOS_SRC_DEBUG_H_

#define SOS_DEBUG 1
#define TMP_DEBUG 1
#define RTN_ON_FAIL 0

#define CB_DEBUG 1

//a little hacky
#if RTN_ON_FAIL
    #define eprintf(...) if (CB_DEBUG) printf(__VA_ARGS__)
#else 
    #define eprintf(...) if (CB_DEBUG) printf(__VA_ARGS__); if (CB_DEBUG) assert(0)
#endif



#endif /* _APPS_SOS_SRC_DEBUG_H_ */
