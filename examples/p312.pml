#define MIN	9	/* first data message to send */
#define MAX	12	/* last  data message to send */
#define FILL	99	/* filler message */

mtype = { ack, nak, err }

proctype transfer1(chan chin, chout)
{	byte o, i, last_i=MIN;

	o = MIN+1;
	do
	:: chin?nak(i) ->
		assert(i == last_i+1);
		chout!ack(o)
	:: chin?ack(i) ->
		if
		:: (o <  MAX) -> o = o+1	/* next */
		:: (o >= MAX) -> o = FILL	/* done */
		fi;
		chout!ack(o)
	:: chin?err(i) ->
		chout!nak(o)
	od
}

proctype transfer2(chan chin, chout)
{   byte o, i, last_i=MIN;

    o = MIN+1;
    do
    :: chin?nak(i) ->
        assert(i == last_i+1);
        chout!ack(o)
    :: chin?ack(i) ->
        if
        :: (o <  MAX) -> o = o+1    /* next */
        :: (o >= MAX) -> o = FILL   /* done */
        fi;
        chout!ack(o)
    :: chin?err(i) ->
        chout!nak(o)
    od
}

proctype channel(chan inp, out)
{	byte md, mt;
	do
	:: inp?mt,md ->
		if
		:: out!mt,md
		:: out!err,0
		fi
	od
}

init
{	chan AtoB = [1] of { byte, byte };
	chan BtoC = [1] of { byte, byte };
	chan CtoA = [1] of { byte, byte };
	atomic {
		run transfer1(AtoB, BtoC);
		run channel(BtoC, CtoA);
		run transfer2(CtoA, AtoB)
	};
	AtoB!err,0	/* start */
}
#define __instances_transfer1 1
#define __instances_transfer2 1
#define __instances_channel 1
