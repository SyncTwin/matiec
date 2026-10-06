{attribute 'label' := 'demo'}
{attribute 'flag'}
{attribute 'pair' := 'k1', 'k2' := 'v2'}
PROGRAM p
VAR
  {attribute 'label' := 'x'} {attribute 'flag'}
  x : INT;
END_VAR
x := x + 1;
END_PROGRAM
