#ifndef SCC_TIME_H
#define SCC_TIME_H

typedef struct timer *mytimer_t;

extern mytimer_t SCCcreateTimer();
extern void SCCdeleteTimer(mytimer_t timer);
extern void SCCresetTimer(mytimer_t timer);
extern void SCCstartTimer(mytimer_t timer);
extern void SCCstopTimer(mytimer_t timer);
extern void SCCreportTimer(mytimer_t timer,char *msg);

#endif

