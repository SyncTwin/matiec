{attribute 'pou_role' := 'feeder'}
FUNCTION_BLOCK Feeder
  VAR_INPUT
    start : BOOL;
    {attribute 'unit' := 'pcs'}
    {attribute 'hmi' := 'rw'}
    lim : INT := 10;
  END_VAR
  VAR_OUTPUT
    busy : BOOL;
    count : INT;
  END_VAR
  VAR
    t1 : TON;
    c1 : CTU;
  END_VAR
  t1(IN := start, PT := T#500ms);
  c1(CU := t1.Q, R := NOT start, PV := lim);
  busy := t1.Q;
  count := c1.CV;
END_FUNCTION_BLOCK

FUNCTION Clamp10 : INT
  VAR_INPUT
    x : INT;
  END_VAR
  IF x > 10 THEN Clamp10 := 10; ELSE Clamp10 := x; END_IF;
END_FUNCTION

PROGRAM Main
  VAR_EXTERNAL
    gReady : BOOL;
  END_VAR
  VAR
    sensor AT %IX0.1 : BOOL;
    motor AT %QX0.0 : BOOL;
    clamp AT %QX0.2 : BOOL := FALSE;
  END_VAR
  VAR
    f1 : Feeder;
    ok : BOOL := TRUE;
    n : INT;
  END_VAR
  INITIAL_STEP Idle:
    Reset(N);
  END_STEP
  STEP Run:
    Drive(S);
    Pulse(L, T#2s);
    Reset(P);
  END_STEP
  STEP Stop:
    Drive(R);
  END_STEP
  TRANSITION (PRIORITY := 1) FROM Idle TO Run
    := sensor AND ok AND gReady;
  END_TRANSITION
  TRANSITION (PRIORITY := 2) FROM Run TO Stop
    := f1.busy;
  END_TRANSITION
  TRANSITION FROM Stop TO Idle
    := NOT sensor;
  END_TRANSITION
  ACTION Reset:
    motor := FALSE;
  END_ACTION
  ACTION Drive:
    motor := TRUE;
    f1(start := motor, lim := 5);
    n := Clamp10(f1.count);
  END_ACTION
  ACTION Pulse:
    {attribute 'note' := 'one shot'}
    clamp := NOT clamp;
  END_ACTION
END_PROGRAM

CONFIGURATION Cfg
  VAR_GLOBAL
    gReady : BOOL := TRUE;
  END_VAR
  RESOURCE Res ON PLC
    TASK Fast(INTERVAL := T#10ms, PRIORITY := 1);
    TASK Slow(INTERVAL := T#100ms, PRIORITY := 5);
    PROGRAM main1 WITH Fast : Main;
  END_RESOURCE
END_CONFIGURATION
