{attribute 'types' := 'plant'}
TYPE
  Speed      : INT := 5;
  Speed2     : Speed;
  Percent    : INT (0..100) := 50;
  Mode       : (Off, Manual, Auto) := Manual;
  Mode2      : Mode;
  Vec3       : ARRAY [1..3] OF REAL := [1.0, 2.0, 3.0];
  Grid       : ARRAY [0..1, 0..2] OF Speed;
  Name20     : STRING[20] := 'none';
  Delay      : TIME := T#1m30s;
  Point : STRUCT
    x    : REAL;
    y    : REAL := 2.5;
    m    : Mode := Auto;
    tmo  : TIME := T#250ms;
  END_STRUCT;
  PointAlias : Point;
END_TYPE

FUNCTION_BLOCK Axis
  VAR_INPUT
    sp : Speed2;
    pt : PointAlias;
  END_VAR
  VAR_OUTPUT
    done : BOOL;
  END_VAR
  VAR
    pct : Percent;
    m   : Mode2;
    d   : Delay := T#1d2h3m4s5.5ms;
    neg : TIME := T#-2s;
    tday : TIME_OF_DAY := TOD#12:00:00;
    v   : Vec3;
    g   : Grid;
    nm  : STRING := 'abc';
    raw : ARRAY [1..2] OF Mode;
    t1  : TON;
  END_VAR
  {attribute 'body' := 'start'}
  t1(IN := TRUE, PT := d);
  {first}
  {second}
  done := t1.Q;
  IF done THEN
    {inner}
    pct := 1;
  END_IF;
END_FUNCTION_BLOCK

PROGRAM Prg
  VAR
    ax  : Axis;
    cnt : Speed;
  END_VAR
  VAR
    outw AT %QW0 : INT;
  END_VAR
  VAR
    spare AT %Q* : BOOL;
  END_VAR
  ax();
  cnt := cnt + 1;
END_PROGRAM

CONFIGURATION Plant
  VAR_GLOBAL
    gMode : Mode := Auto;
    gTrig : BOOL;
  END_VAR
  RESOURCE Cpu ON PLC
    VAR_GLOBAL
      gDelay : Delay;
      gAx    : Axis;
    END_VAR
    TASK Fast(INTERVAL := T#10ms, PRIORITY := 1);
    TASK Slow(INTERVAL := TIME#1.5s, PRIORITY := 5);
    TASK Event(SINGLE := gTrig, PRIORITY := 3);
    PROGRAM p1 WITH Fast : Prg (gAx WITH Slow);
    PROGRAM p2 : Prg;
  END_RESOURCE
  VAR_ACCESS
    AccCnt  : Cpu.p1.cnt : Speed READ_WRITE;
    AccMode : gMode : Mode READ_ONLY;
    AccOut  : Cpu.p1.outw : INT;
    AccIn   : %IX7.1 : BOOL READ_ONLY;
  END_VAR
  VAR_CONFIG
    Cpu.p1.spare AT %QX5.0 : BOOL := TRUE;
    Cpu.p1.ax.d : Delay := T#3s;
  END_VAR
END_CONFIGURATION
