/* Peterson's algorithm for N processes - Lynch, p. 284 */

#define MORE_STATES

#ifndef N
#define N	3
#endif

byte turn[N], flag[N];
byte ncrit;	/* auxiliary variable to check mutex */

#ifdef MORE_STATES
	byte p_cnt;	/* increase the nr of reachable states a bit more */
#endif

active [N] proctype user()
{	byte j, k;
	/* without never claims, _pid's are: 0 .. N-1 */
again:
	k = 0;	/* count max N-1 rounds of competition */
	do
	:: k < N-1 ->
		flag[_pid] = k;
		turn[k] = _pid;

		j = 0;		/* for all j != _pid */
		do
		:: j == _pid ->
			j++
		:: else ->
			if
			:: j < N ->
#ifdef ORIG
	/* -DNOREDUCE:  1.1M states - depth 1.4M - time 37s */
				(flag[j] < k || turn[k] != _pid);
#else
	/* -DNOREDUCE: 16.8M states - depth 2.8M - time 65s */
				do
				:: flag[j] < k -> break
				:: else ->
					if
					:: turn[k] != _pid -> break
					:: else
					fi
				od;
#endif
				j++
			:: else  ->
				break
			fi
		od;
		k++
	:: else ->	/* survived all n-1 rounds */
		break
	od;

	ncrit++;
	assert(ncrit == 1);	/* critical section */
#ifdef MORE_STATES
	p_cnt = p_cnt | (1<<_pid);	/* pids are 1..N, so this is 2..2^N */
#endif
	ncrit--;

	flag[_pid] = 0;
	goto again
}
#if 1
#define p	(p_cnt >= (2+4+8))	/* assuming N >= 3 */

never {    /* []<> p */
T0_init:
        if
        :: ((p)) -> goto accept_S9
        :: (1) -> goto T0_init
        fi;
accept_S9:
        if
        :: (1) -> goto T0_init
        fi;
}
#endif
