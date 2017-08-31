
# Algoritmic backends:
# --------------------

# for every backend create params, options and a model
set backends [dict create]
# 1 row for every param and a list of possible values
dict set backends mc params "--strategy=" {dfs bfs sbfs}
dict set backends mc params "--state=" {tree table cleary-tree}
dict set backends mc options {"-z6" "--no-ref" "--perm=rr" "--perm=dynamic" ""}


# 1 row for every param and a list of possible values
dict set backends sym params "--order=" {bfs-prev bfs chain-prev chain par par-prev}
dict set backends sym params "--saturation=" {none sat-like sat-loop sat-fix sat}
# internal and BuDDy
dict set backends sym params "--vset=" {"ldd --lace-workers=1" "fdd --lace-workers=1" "ldd64 --lace-workers=1" "lddmc --lace-workers=2 --sylvan-sizes=19,19,19,19" "sylvan --lace-workers=2 --sylvan-bits=32 --sylvan-sizes=22,22,22,22"}
dict set backends sym options {"-g" "-rga" "-g -rga" ""}

# 1 row for every param and a list of possible values
dict set backends seq params "--strategy=" {dfs bfs scc}
dict set backends seq params "--state=" {tree table vset}
dict set backends seq options {""}

# 1 row for every param and a list of possible values
dict set backends dist params "" {""}
dict set backends dist options {""}


proc test_forall_params { params_dict idx command } {

    set num_params [dict size $params_dict]
    set params [dict keys $params_dict]
    set cur_param [lindex $params $idx]
    set command_list [list]

    foreach option [dict get $params_dict $cur_param] {
        # check if last param in params
        if { $idx < $num_params-1} {
            set new_index [expr {$idx+1}]
            set result_list [test_forall_params $params_dict $new_index "$command $cur_param$option" ]
            set command_list [concat $command_list $result_list]
        } else {
            lappend command_list "$command $cur_param$option"
        }        
    }

    return $command_list
}

# alg_be: list of paths to binairies to be tested
# langs: dict with options for language frontends (model, expected output etc.)
# backends: dict with options for algorithmic backends
proc run_test_for_alg_backends { alg_be langs backends} {
    global EXAMPLES_SRC_PATH
    global EXAMPLES_BUILD_PATH

    foreach path $alg_be {
        set testcounter 1
        set tokens [split $path "/"]
        set command [lindex $tokens end]
        set be [lindex [split $command "-"] 1]
        set lang_fe [lindex [split $command "2"] 0]
        
        # the model is a required value
        if { [dict exists $langs $lang_fe model] } {
            set model [dict get $langs $lang_fe model]

            if { [file exists "$model" ] } {
            	set model "$model"
            } elseif { [file exists "$EXAMPLES_SRC_PATH/$model" ] } {
		 set model "$EXAMPLES_SRC_PATH/$model"
            } elseif { [file exists "$EXAMPLES_BUILD_PATH/$model" ] } {
		 set model "$EXAMPLES_BUILD_PATH/$model"
            } else {
                 fail "Model not found: $model"
		 continue
            }
            # if exists, execute the precommand
            # i.e. can be used to generate models
            if { [dict exists $langs $lang_fe precommands] } {
                set precommands [dict get $langs $lang_fe precommands]
                foreach precmd $precommands {
                    puts "Executing precommand: $precmd"
                    eval exec $precmd
                }
            }

            set command_list [list]

            if { [ dict exists $backends $be ] } {
                set options [ dict get $backends $be options ]
                set params [dict create]
                set params [ dict get $backends $be params ]


                for {set i 0} {$i < [llength $options]} {incr i} {
                    set option [lindex $options $i]
                    set command_list [concat $command_list [test_forall_params $params 0 "$path $option"]]
                }

                set lang_option ""
                if { [dict exists $langs $lang_fe options $be ] } {
                    set lang_option [dict get $langs $lang_fe options $be]
                }
                
                foreach cmd $command_list {
                    set command "$cmd $lang_option $model"
                    runmytest $testcounter $command [dict get $langs $lang_fe exp_output ]
                }

            } else {
                note "No test defined for backend: $command $be"
            }
        } else {
	    #note "No model defined for language frontend $lang_fe"
        }
    }
}

