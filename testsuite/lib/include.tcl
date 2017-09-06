# include.tcl
# File for putting general test procedures

# Models should be able to complete within the timeout value.
set timeout 120

proc makeAbsolute {pathname} {
    file join [pwd] $pathname
}

set LTSMIN_SRCDIR "[makeAbsolute $srcdir]/.."
set LTSMIN_BUILDDIR "[makeAbsolute $base_dir]/.."


# The directory containing all the models used for testing.
set EXAMPLES_SRC_PATH "$LTSMIN_SRCDIR/examples"
set EXAMPLES_BUILD_PATH "$LTSMIN_BUILDDIR/examples"

# filter: filter a specific (list of) backend(s): {mc,sym,seq,dist}
proc find_alg_backends { filter } {
    global LTSMIN_BUILDDIR
    global base_dir
    set backends [list]
    set lts_backends [glob -directory "$base_dir/../src" -type d pins2lts-$filter ]
    foreach dir $lts_backends {
        set lts_bins [glob -nocomplain -directory $dir -type f *2lts-$filter ]
        foreach path $lts_bins {
            lappend backends $path
        }
        set lts_bins [glob -nocomplain -directory $dir/gcc -type f *2lts-$filter ]
        foreach path $lts_bins {
            lappend backends $path
        }
        set lts_bins [glob -nocomplain -directory $dir/mpicc -type f *2lts-$filter ]
        foreach path $lts_bins {
            lappend backends $path
        }
    }

    return $backends
}

proc find_lang_frontends { filter } {
    global base_dir
    set frontends [list]
    set lts_backends [glob -directory "$base_dir/../src" -type d pins2lts* ]
    foreach dir $lts_backends {
        set lts_bins [glob -nocomplain -directory $dir -type f $filter]
        foreach path $lts_bins {
            lappend frontends $path
        }
    }

    return $frontends
}

proc runmytest { test_name command_line exp_output} {
    send_user "starting $command_line\n"

    # NOTE: this is ugly. If the exp_output is not set, put an unfindable string in it.
    set unfindable_string "adhadkhaslkdLKHLKHads876"

    if { [string length $exp_output] == 0 } {
        set exp_output $unfindable_string
    }

    set PID [ eval spawn $command_line ]

    set expected false
    match_max 1000000
    expect {

        # expected last line when execution succeeds
        "writing output took" {
            exec sync
            pass "Program finished\n"
        }

        "not enough arguments" {
            fail "Not enough arguments given for $command_line"
        }

        "File does not exist:" {
            fail "Argument file does not exists"
        }


        "Zobrist and treedbs is not implemented" {
            xfail "The combination of zobrist and treedbs is not implemented";
            catch { close }
            wait
            return
        }
	    
        "unimplemented combination --strategy=bfs, --state=table" {
            xfail "unimplemented combination --strategy=bfs, --state=table";
            catch { close }
            wait
            return
	    }

        "Decision diagram package does not support least fixpoint" {
            xfail "Decision diagram package does not support least fixpoint";
            catch { close }
            wait
            return
        }
        
        "SCC search only works in combination with an accepting state label" {
            xfail "SCC search only works in combination with an accepting state label";
            catch { close }
            wait
            return
        }
        
        "BCG support was not enabled at compile time." {
            xfail "BCG support was not enabled at compile time."
            catch { close }
            wait
            return
        }
        
        "cannot write state labels to AUT file" {
            xfail "cannot write state labels to AUT file"
            catch { close }
            wait
            return
        }
        
        "Vector set implementation does not support vset_join operation." {
            xfail "Vector set implementation does not support vset_join operation."
            catch { close }
            wait
            return
        }
        
        "guard-splitting not supported with saturation=" {
            xfail "guard-splitting not supported with saturation="
            catch { close }
            wait
            return
        }
        
        "No long next-state function implemented for this language module (--pins-guards)." {
            xfail "No long next-state function implemented for this language module (--pins-guards)."
            catch { close }
            wait
            return
        }
        
        "Cannot apply branching bisimulation to an LTS with state labels." {
            xfail "Cannot apply branching bisimulation to an LTS with state labels."
            catch { close }
            wait
            return
        }
        
        "Cleary tree not supported in combination with error trails or the MCNDFS algorithms." {
            xfail "Cleary tree not supported in combination with error trails or the MCNDFS algorithms."
            catch { close }
            wait
            return
        }
        
        # Check for any warning messages in the output first
        Warning {
            fail "$test_name: warning: $expect_out(buffer)"
        }

        # Check for any error messages
        ERROR {
            fail "$test_name: error: $expect_out(buffer)"
        }

        "error" {
            fail "An error message was encountered in the application output.";
        }

        timeout {
            fail "Program takes to long to execute"
            exec kill -9 $PID
            return
        }

        -re $exp_output {
            exec sync
            pass "Expected output $exp_output found"
        }
	    
        full_buffer {
            puts "\n full buffer hit"
            exit -1
        }
        
        eof {
            # only fail if an exp_output is set, otherwise let the exit code decide
            if { $exp_output != $unfindable_string } {
                fail "Program ended without outputting: $exp_output"
            }        
        }

    }
    catch { close }
    set result [wait]
    set exit_code [lindex $result 3]

    #puts "DEBUG: exit_code: $exit_code"
    switch $exit_code {
        0   {
                if { $exp_output != $unfindable_string } {
                    return
            }
                pass  "$test_name: Program exited with a zero exit code" 
            }
        1   { xfail "$test_name: Program exited with LTSMIN_COUNTER_EXAMPLE" }
        127 { fail "$test_name: Program exited with 127. Probably unable to find external lib. Run ldconfig or update your \$LD_LIBRARY_PATH" }
        255 { fail  "$test_name: Program exited with LTSMIN_EXIT_ERROR" }
        default { fail "$test_name: Program exited with a unknown exit code: $exit_code" }
    }
}

# create a list with for every bin the path
set binpaths(spins-jar) "$LTSMIN_SRCDIR/spins/spins.jar"
set binpaths(ltsmin-compare) "$base_dir/../src/ltsmin-compare/ltsmin-compare"
set binpaths(ltsmin-convert) "$base_dir/../src/ltsmin-convert/ltsmin-convert"
set binpaths(ltsmin-printtrace) "$base_dir/../src/ltsmin-printtrace/ltsmin-printtrace"
set binpaths(spins) "$LTSMIN_SRCDIR/src/scripts/spins"
set binpaths(out) "out"

set bins [find_alg_backends "{seq,mc,dist,sym}"]
foreach path $bins {
    set bin [lindex [split $path "/"] end]
    set binpaths($bin) $path
}

proc compile_promela { prom_models } {
    global binpaths
    global EXAMPLES_SRC_PATH

    if { ! [file exists "$binpaths(spins-jar)" ] } {
	 fail "Cannot find spins binary in $binpaths(spins-jar)"
	 exit 0
    }

    foreach prom_model $prom_models {
        puts "Executing precommand: '$binpaths(spins) $EXAMPLES_SRC_PATH/$prom_model'"
        set rc [catch { exec $binpaths(spins) -o $EXAMPLES_SRC_PATH/$prom_model } msg ]

        # check for exit code, and whether there is output on stderr
        if { $rc != 0 } {
            fail "Failed executing precommand"
            puts "this is what I got on stderr: $msg"
            exit $rc
        }
    }
    return true
}

proc compile_DVE { DVE_models } {
    global EXAMPLES_SRC_PATH

    foreach DVE_model $DVE_models {
        puts "Executing precommand: 'divine compile -l $EXAMPLES_SRC_PATH/$DVE_model'"
        set rc [catch { exec divine compile -l $EXAMPLES_SRC_PATH/$DVE_model } msg ]

        # check for exit code, and whether there is output on stderr
        if { $rc != 0 } {
            fail "Failed executing precommand"
            puts "this is what I got on stderr: $msg"
            exit $rc
        }
    }
    return true
}

proc start_ProB { model } {
    global EXAMPLES_SRC_PATH

    puts "Executing precommand: 'probcli $EXAMPLES_SRC_PATH/$model -ltsmin'"
    set PID [exec probcli $EXAMPLES_SRC_PATH/$model -ltsmin &]

    while { ! [file exists /tmp/ltsmin.probz] } {
        sleep 1
    }
    return $PID
}
