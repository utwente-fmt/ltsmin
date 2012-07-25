logTO(){
  LOG_FILE=$1
  echo date > $LOG_FILE
}

goodCMD(){
  echo "running $*" >> $LOG_FILE
  echo "  running (good) $*"
  if ! "$@" >> $LOG_FILE 2>&1
  then fail "unexpected failure" ; fi
}

badCMD(){
  echo "running $*" >> $LOG_FILE
  echo "  running (bad) $*"
  if "$@" >> $LOG_FILE 2>&1
  then fail "unexpected success" ; fi
}

mustSay(){
  if ! test -f $LOG_FILE ; then return ; fi
  egrep -q "$1" $LOG_FILE || fail "log file $LOG_FILE does not contain [$1]"
}

compareFiles(){
  dir1=$1
  dir2=$2
  shift
  shift
  for f in $* ; do
    if ! cmp -s $dir1/$f $dir2/$f
    then fail "Files $dir1/$f and $dir2/$f are different" ; fi
  done
}

