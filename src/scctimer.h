#ifndef SCC_TIME_H
#define SCC_TIME_H

#include <runtime.h>

typedef struct timer *mytimer_t;

extern mytimer_t SCCcreateTimer();
extern void SCCdeleteTimer(mytimer_t timer);
extern void SCCresetTimer(mytimer_t timer);
extern void SCCstartTimer(mytimer_t timer);
extern void SCCstopTimer(mytimer_t timer);
extern void SCCreportTimer(mytimer_t timer,char *msg);
extern void SCClogTimer(log_t log,mytimer_t timer,char *msg);
extern float SCCrealTime(mytimer_t timer);

#endif

