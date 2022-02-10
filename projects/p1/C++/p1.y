%{
#include <cstdio>
#include <list>
#include <vector>
#include <map>
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

map <string, Value*> symbolTable;

// Create a function which adds a value to the symbolTable
void addToSymbolTable(string name, Value* value) {
  symbolTable[name] = value;
}

// Create a function which returns a value from the symbolTable
Value* getFromSymbolTable(string name) {
  return symbolTable[name];
}

// Create a function which checks if key is in the symbolTable and returns true if it is
bool isInSymbolTable(string name) {
  return symbolTable.find(name) != symbolTable.end();
}

// Create a function to retrive a bit from an integer
Value* getBit(Value* value, int bit) {
  return Builder.CreateAnd(Builder.CreateLShr(value, bit), Builder.getInt32(1));
}

// Get lowest bit from integer
Value* getLowestBit(Value* value) {
  return getBit(value, 0);
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

final: FINAL expr ENDLINE
{
  // Create a return instruction returning the value of the expression
  Builder.CreateRet($2);
}
;

statements_opt: %empty
            | statements;

statements:   statement 
            | statements statement 
;

statement: bitslice_lhs ASSIGN expr ENDLINE
{
  string s($1);
  symbolTable[s] = $3;
}
| SLICE field_list ENDLINE
;

field_list : field_list COMMA field
           | field
;

field : ID COLON expr
| ID LBRACKET expr RBRACKET COLON expr
// 566 only below
| ID
;

expr: bitslice
{
  // return bitslice
  $$ = $1;
}
| expr PLUS expr
| expr MINUS expr
| expr XOR expr
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

bitslice: ID
{
    $$ = getFromSymbolTable((string)$1);
}
| NUMBER
{
  // Convert Number to LLVM type and return it
  $$ = Builder.getInt32($1);
}
| bitslice_list
{
  // return bitslice_list
  $$ = $1;
}
| LPAREN expr RPAREN
| bitslice NUMBER
{
  // Use Offset here
  $$ = getBit($1,$2);
}
| bitslice DOT ID
// 566 only
| bitslice LBRACKET expr RBRACKET
| bitslice LBRACKET expr COLON expr RBRACKET
;

bitslice_list: LBRACE bitslice_list_helper RBRACE
{
  // return bitslice_list_helper
  $$ = $2;
}
;

bitslice_list_helper:  bitslice
{
  // return bitslice
  $$ = getLowestBit($1);
}
| bitslice_list_helper COMMA bitslice
{
  // Left shift $1 by 1, add $3 to it
  $$ = Builder.CreateAdd(Builder.CreateShl($1, Builder.getInt32(1), "shl"), $3, "add");
}
;

bitslice_lhs: ID
{
  $$ = $1;
}
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
