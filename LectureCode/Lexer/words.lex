%{
#include <stdio.h>
#include <stdbool.h>

bool starts_with_alpha = false;
bool contains_lowercase = false;
bool contains_uppercase = false;
bool contains_digits = false;
bool contains_specials = false;
bool contains_arbitary = false;
bool more_20 = false;

void check_valid_pword ()
{
  bool passwordValid = starts_with_alpha && contains_lowercase && contains_uppercase && contains_digits && contains_specials && !contains_arbitary && !more_20;
  printf("Start %d, L %d, U %d, Num %d, Sp %d, Arb %d, >20 %d", starts_with_alpha, contains_lowercase, contains_uppercase, contains_digits, contains_specials, contains_arbitary, more_20);
  passwordValid == true ? printf("\nValid\n") : printf("\nnope\n");
  
  starts_with_alpha = false;
  contains_lowercase = false;
  contains_uppercase = false;
  contains_digits = false;
  contains_specials = false;
  contains_arbitary = false;
  more_20 = false;
}

%}


%option noyywrap


%%

\n                  {check_valid_pword();}
^[a-z]              {starts_with_alpha = true; contains_lowercase = true;}
^[A-Z]              {starts_with_alpha = true; contains_uppercase = true;}
[a-z]               {contains_lowercase = true;}
[A-Z]               {contains_uppercase = true;}
[0-9]               {contains_digits = true;}
[#$%^&!_]           {contains_specials = true;}
[^a-zA-Z0-9#$%^&!_] {contains_arbitary = true;}
^.{21,}$            {more_20 = true;}

%%

/*
[a-zA-Z][a-zA-Z0-9#$%^&!_]+{4,20} {printf("Match\n");}

 */
int main( int argc, char **argv )
{
  ++argv, --argc;  /* skip over program name */
  if ( argc > 0 )
    yyin = fopen( argv[0], "r" );
  else
    yyin = stdin;
  yylex();

  return 0;
}

