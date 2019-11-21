MACHINE MutexSimple
VARIABLES cs, wait, finished
INVARIANT
 cs:BOOL & wait: NATURAL & finished:NATURAL
INITIALISATION cs := FALSE || wait := 2 || finished := 0
OPERATIONS
  Enter = SELECT cs=FALSE & wait>0 THEN  cs := TRUE || wait := wait-1 END;
  Exit = SELECT cs=TRUE THEN cs := FALSE || finished := finished+1 END;
  Leave = BEGIN cs := FALSE END;
  CS_Active = SELECT cs=TRUE THEN skip END;
  Restart = SELECT finished>0 THEN wait := wait+1 || finished := finished-1 END
END
/*
Read/write dependencies:
      0 
   0: www+
   1: ++-r
   2: +-+r
   3: w--r
   4: r--r
   5: -++r
*/
