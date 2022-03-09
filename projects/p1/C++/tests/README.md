## Test5
#### in none

#### slice e:4, d:3, c:2, b:1, a:0
ID COLON expr
field
ID COLON expr
field_list COMMA field
ID COLON expr
field_list COMMA field
ID COLON expr
field_list COMMA field
ID COLON expr
field_list COMMA field
SLICE field_list ENDLINE

#### a = {1,0,0,1,0}

#### b = {a.a,a.b,a.c,a.d,a.e}
bitslice DOT ID
bitslice DOT ID
bitslice DOT ID
bitslice DOT ID
bitslice DOT ID

#### final b
; ModuleID = 'in'
source_filename = "in"

define i32 @in() {
entry:
  ret i32 270
}