TYPE
  MY_INT : INT;
END_TYPE

FUNCTION_BLOCK COUNTER_FB
  VAR_INPUT
    reset : BOOL;
  END_VAR
  VAR_OUTPUT
    out : INT;
  END_VAR
  VAR
    count : INT := 0;
  END_VAR

  IF reset THEN
    count := 0;
  ELSE
    count := count + 1;
  END_IF;
  
  out := count;
END_FUNCTION_BLOCK

PROGRAM my_program
  VAR
    my_counter : COUNTER_FB;
    reset_trigger : BOOL;
    current_value : INT;
  END_VAR

  my_counter(reset := reset_trigger);
  current_value := my_counter.out;
END_PROGRAM

CONFIGURATION config
  RESOURCE res ON PLC
    TASK task0(INTERVAL := T#20ms, PRIORITY := 0);
    PROGRAM instance0 WITH task0 : my_program;
  END_RESOURCE
END_CONFIGURATION
