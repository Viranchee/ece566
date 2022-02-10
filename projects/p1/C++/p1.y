%{
#include <cstdio>
#include <list>
#include <vector>
#include <map>
#include <unordered_map>
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/FileSystem.h"

using namespace llvm;
using namespace std;

// Need for parser and scanner
extern FILE *yyin;
int yylex();
void yyerror(const char*);
int yyparse();
 
// Needed for LLVM
string funName;
Module *M;
LLVMContext TheContext;
IRBuilder<> Builder(TheContext);

// Dictionary
unordered_map <string, Value*> dictionary;

// Dictionary helper functions

// Create a function which adds a value to the dictionary
void addToDictionary(string name, Value* value) {
  dictionary[name] = value;
}

// Create a function which returns a value from the dictionary
Value* getFromdictionary(string name) {
  return dictionary[name];
}

// Create a function which checks if key is in the dictionary and returns true if it is
bool isIndictionary(string name) {
  return dictionary.find(name) != dictionary.end();
}

// Bit manipulation instructions

// Create a function to retrive a bit from an integer
Value* getBit(Value* value, int bit) {
  return Builder.CreateAnd(Builder.CreateLShr(value, bit), Builder.getInt32(1));
}

// Get lowest bit from integer
Value* getLowestBit(Value* value) {
  return getBit(value, 0);
}
Value* do_leftshiftbyn_add(Value* value, Value* shift, Value* add) {
  return Builder.CreateAdd(Builder.CreateShl(value, shift), add);
}
Value* do_leftshiftbyn_add(Value* value, int shift, Value* add) {
  // Left shift $1 by 1, add $3 to it
  return do_leftshiftbyn_add(value, Builder.getInt32(shift), add);
}

void do_assign_addtodict(char* key, Value* value) { 
  string s(key);
  addToDictionary(s, value);
}

%}

%union {
  vector<string> *params_list;
  int num;
  char* id;
  Value *val;
}

/*%define parse.trace*/

%type <params_list> params_list

%type <val> expr
%type <val> final
%type <val> bitslice
%type <val> bitslice_list
%type <val> bitslice_list_helper
%type <id> bitslice_lhs

%type <val> field
%type <val> field_list

%type <val> statement
%type <val> statements
%type <val> statements_opt

%token IN FINAL SLICE
%token ERROR
%token <num> NUMBER
%token <id> ID
%token BINV INV PLUS MINUS XOR AND OR MUL DIV MOD
%token COMMA ENDLINE ASSIGN LBRACKET RBRACKET LPAREN RPAREN NONE COLON
%token LBRACE RBRACE DOT
%token REDUCE EXPAND

%precedence BINV
%precedence INV
%left PLUS MINUS OR
%left MUL DIV AND XOR MOD

%start program

%%

program: inputs statements_opt final
{
  YYACCEPT;
}
;

inputs:   IN params_list ENDLINE
{  
  std::vector<Type*> param_types;
  for(auto s: *$2)
    {
      param_types.push_back(Builder.getInt32Ty());
    }
  ArrayRef<Type*> Params (param_types);
  
  // Create int function type with no arguments
  FunctionType *FunType = 
    FunctionType::get(Builder.getInt32Ty(),Params,false);

  // Create a main function
  Function *Function = Function::Create(FunType,GlobalValue::ExternalLinkage,funName,M);

  int arg_no=0;
  for(auto &a: Function->args()) {
    // iterate over arguments of function
    // match name to position
  }
  
  //Add a basic block to main to hold instructions, and set Builder
  //to insert there
  Builder.SetInsertPoint(BasicBlock::Create(TheContext, "entry", Function));

}
| IN NONE ENDLINE
{ 
  // Create int function type with no arguments
  FunctionType *FunType = 
    FunctionType::get(Builder.getInt32Ty(),false);

  // Create a main function
  Function *Function = Function::Create(FunType,  
         GlobalValue::ExternalLinkage,funName,M);

  //Add a basic block to main to hold instructions, and set Builder
  //to insert there
  Builder.SetInsertPoint(BasicBlock::Create(TheContext, "entry", Function));
}
;

params_list: ID
{
  $$ = new vector<string>;
  // add ID to vector
}
| params_list COMMA ID
{
  // add ID to $1
}
;

final:                FINAL expr ENDLINE { Builder.CreateRet($$); }
                      ;

statements_opt:       %empty
                      | statements;

statements:           statement
                      | statements statement 
                      ;

statement:            bitslice_lhs ASSIGN expr ENDLINE { do_assign_addtodict($1, $3); }
                      | SLICE field_list ENDLINE
                      ;

field_list :          field_list COMMA field
                      | field
;

field :                 ID COLON expr
                      | ID LBRACKET expr RBRACKET COLON expr
// 566 only below
                      | ID
                      ;

expr:                 bitslice  { $$ = $1; }
                      | expr PLUS expr
                      | expr MINUS expr
                      | expr XOR expr {$$ = Builder.CreateXor($1, $3);}
                      | expr AND expr
                      | expr OR expr
                      | INV expr
                      | BINV expr
                      | expr MUL expr
                      | expr DIV expr
                      | expr MOD expr
/* 566 only */
                      | REDUCE AND LPAREN expr RPAREN
                      | REDUCE OR LPAREN expr RPAREN
                      | REDUCE XOR LPAREN expr RPAREN
                      | REDUCE PLUS LPAREN expr RPAREN
                      | EXPAND LPAREN expr RPAREN
                      ;

bitslice:             ID { $$ = getFromdictionary((string)$1);}
                      | NUMBER { $$ = Builder.getInt32($1);}
                      | bitslice_list { $$ = $1;}
                      | LPAREN expr RPAREN { $$ = $2;}
                      | bitslice NUMBER { $$ = getBit($1,$2);}
                      | bitslice DOT ID
// 566 only
                      | bitslice LBRACKET expr RBRACKET
                      | bitslice LBRACKET expr COLON expr RBRACKET
                      ;

bitslice_list: LBRACE bitslice_list_helper RBRACE { $$ = $2;}
                      ;

bitslice_list_helper:  bitslice { $$ = getLowestBit($1); }
                      | bitslice_list_helper COMMA bitslice { $$ = do_leftshiftbyn_add($1,1,$3); }
;

bitslice_lhs:         ID { $$ = $1; }
                      | bitslice_lhs NUMBER
                      | bitslice_lhs DOT ID
// 566 only
                      | bitslice_lhs LBRACKET expr RBRACKET
                      | bitslice_lhs LBRACKET expr COLON expr RBRACKET
;

%%

unique_ptr<Module> parseP1File(const string &InputFilename)
{
  funName = InputFilename;
  if (funName.find_last_of('/') != string::npos)
    funName = funName.substr(funName.find_last_of('/')+1);
  if (funName.find_last_of('.') != string::npos)
    funName.resize(funName.find_last_of('.'));
    
  //errs() << "Function will be called " << funName << ".\n";
  
  // unique_ptr will clean up after us, call destructor, etc.
  unique_ptr<Module> Mptr(new Module(funName.c_str(), TheContext));

  // set global module
  M = Mptr.get();
  
  /* this is the name of the file to generate, you can also use
     this string to figure out the name of the generated function */
  yyin = fopen(InputFilename.c_str(),"r");

  //yydebug = 1;
  if (yyparse() != 0)
    // errors, so discard module
    Mptr.reset();
  else
    // Dump LLVM IR to the screen for debugging
    M->print(errs(),nullptr,false,true);
  
  return Mptr;
}

void yyerror(const char* msg)
{
  printf("%s\n",msg);
}
