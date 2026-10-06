FUNCTION_BLOCK fb
VAR_INPUT i : INT; END_VAR
VAR_OUTPUT
  {attribute 'label' := 'o'}
  o : INT;
END_VAR
o := i;
END_FUNCTION_BLOCK
