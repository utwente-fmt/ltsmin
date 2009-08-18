#ifndef SCC_TIME_H
#define SCC_TIME_H

typedef struct timer *mytimer_t;

extern mytimer_t createTimer();
extern void deleteTimer(mytimer_t timer);
extern void resetTimer(mytimer_t timer);
extern void startTimer(mytimer_t timer);
extern void stopTimer(mytimer_t timer);
extern void reportTimer(mytimer_t timer,char *msg);

#endif

