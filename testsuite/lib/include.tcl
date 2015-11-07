# include.tcl
# File for putting general test procedures

# Models should be able to complete within the timeout value.
set timeout 120

proc makeAbsolute {pathname} {
    file join [pwd] $pathname
}

set LTSMIN_SRCDIR "[makeAbsolute $srcdir]/.."

# The directory containing all the models used for testing.
set EXAMPLES_PATH "$LTSMIN_SRCDIR/examples"

# filter: filter a specific (list of) backend(s): {mc,sym,seq,dist}
proc find_alg_backends { filter } {
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
            catch { exp_close }
            return
        }
	    
        "unimplemented combination --strategy=bfs, --state=table" {
    	    xfail "unimplemented combination --strategy=bfs, --state=table";
    	    catch { exp_close }
    	    return
	    }

        "Decision diagram package does not support least fixpoint" {
    	    xfail "Decision diagram package does not support least fixpoint";
    	    catch { exp_close }
    	    return
        }
        
        "SCC search only works in combination with an accepting state label" {
            xfail "SCC search only works in combination with an accepting state label";
            catch { exp_close }
            return
        }
        
        "BCG support was not enabled at compile time." {
            xfail "BCG support was not enabled at compile time."
            catch { exp_close }
            return
        }
        
        "cannot write state labels to AUT file" {
            xfail "cannot write state labels to AUT file"
            catch { exp_close }
            return
        }
        
        "Vector set implementation does not support vset_join operation." {
            xfail "Vector set implementation does not support vset_join operation."
            catch { exp_close }
            return
        }
        
        "guard-splitting not supported with saturation=" {
            xfail "guard-splitting not supported with saturation="
            catch { exp_close }
            return
        }
        
        "No long next-state function implemented for this language module (--pins-guards)." {
            xfail "No long next-state function implemented for this language module (--pins-guards)."
            catch { exp_close }
            return
        }
        
        "Cannot apply branching bisimulation to an LTS with state labels." {
            xfail "Cannot apply branching bisimulation to an LTS with state labels."
            catch { exp_close }
            return
        }
        
        "Cleary tree not supported in combination with error trails or the MCNDFS algorithms." {
            xfail "Cleary tree not supported in combination with error trails or the MCNDFS algorithms."
            catch { exp_close }
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
    catch { exp_close }
    set result [exp_wait]
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
set binpaths(spins) "$base_dir/../src/scripts/spins"
set binpaths(out) "out"

set bins [find_alg_backends "{seq,mc,dist,sym}"]
foreach path $bins {
    set bin [lindex [split $path "/"] end]
    set binpaths($bin) $path
}

proc compile_promela { prom_models } {
    global binpaths
    global EXAMPLES_PATH

    if { ! [file exists "$binpaths(spins-jar)" ] } {
	 fail "Cannot find spins binary in $binpaths(spins-jar)"
	 exit 0
    }

    foreach prom_model $prom_models {
        set commands {"$binpaths(spins) $EXAMPLES_PATH/$prom_model"}
# "mv $prom_model.spins $EXAMPLES_PATH/"
        foreach command $commands {
            puts [subst "Executing precommand: '$command'"]
            eval exec $command
        }
    }
    return true
}

