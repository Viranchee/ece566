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
unordered_map <string, Value*> values;
unordered_map <string, Value*> slices;

// Bit manipulation instructions

// Get range of bits from integer
Value* getRange(Value* value, int start, int length) {
  return Builder.CreateLShr(Builder.CreateAnd(value, Builder.getInt32(pow(2, length) - 1)), start);
}

// Create a function to retrive a bit from llvm builder value
Value* getBit(Value* value, Value* position) {
  return Builder.CreateAnd(Builder.CreateLShr(value, position), Builder.getInt32(1));
}
// Get lowest bit from integer
Value* getLowestBit(Value* value) {
  return getBit(value, Builder.getInt32(0));
}
Value* do_leftshiftbyn_add(Value* value, Value* shift, Value* add) {
  return Builder.CreateAdd(Builder.CreateShl(value, shift), add);
}
Value* do_leftshiftbyn_add(Value* value, int shift, Value* add) {
  // Left shift $1 by 1, add $3 to it
  return do_leftshiftbyn_add(value, Builder.getInt32(shift), add);
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

program:              inputs statements_opt final { YYACCEPT;}

inputs:               IN params_list ENDLINE
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
                          // get first element from vector $2, 
                          values[$2->at(arg_no)] = &a;
                          // match name to position
                          arg_no++;
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

params_list:          ID
                      {
                        $$ = new vector<string>;
                        // add ID to vector
                        $$->push_back(string($1));
                      }
                      | params_list COMMA ID
                      {
                        // add ID to $1
                        $$->push_back(string($3));
                      }
                      ;

final:                FINAL expr ENDLINE { Builder.CreateRet($2); }
                      ;

statements_opt:       %empty
                      | statements;

statements:           statement
                      | statements statement 
                      ;

statement:            bitslice_lhs ASSIGN expr ENDLINE { values[string($1)] = $3; }
                      | SLICE field_list ENDLINE { printf("SLICE field_list ENDLINE\n"); }
                      ;

field_list :          field_list COMMA field { printf("field_list COMMA field\n"); }
                      | field { printf("field\n"); }
                      ;

field :               ID COLON expr { slices[(string)$1] = $3; }
                      | ID LBRACKET expr RBRACKET COLON expr { printf("ID LBRACKET expr RBRACKET COLON expr\n"); }
// 566 only below
                      | ID { printf("ID\n"); }
                      ;

expr:                 bitslice  { $$ = $1; }
                      | expr PLUS expr    { $$ = Builder.CreateAdd($1, $3); }
                      | expr MINUS expr   { $$ = Builder.CreateSub($1, $3); }
                      | expr XOR expr     { $$ = Builder.CreateXor($1, $3); }
                      | expr AND expr     { $$ = Builder.CreateAnd($1, $3); }
                      | expr OR expr      { $$ = Builder.CreateOr($1, $3); }
                      | INV expr          { $$ = Builder.CreateNot($2); }
                      | BINV expr 
                      {
                        // Flip the LSB, by using XOR operation with 1
                        Value* one = Builder.getInt32(1);
                        $$ = Builder.CreateXor($2, one);
                      }
                      | expr MUL expr     {$$ = Builder.CreateMul($1, $3);}
                      | expr DIV expr     {$$ = Builder.CreateSDiv($1, $3);}
                      | expr MOD expr     {$$ = Builder.CreateSRem($1, $3);}
/* 566 only */
                      | REDUCE AND LPAREN expr RPAREN { printf("REDUCE AND LPAREN expr RPAREN\n"); }
                      | REDUCE OR LPAREN expr RPAREN { printf("REDUCE OR LPAREN expr RPAREN\n"); }
                      | REDUCE XOR LPAREN expr RPAREN { printf("REDUCE XOR LPAREN expr RPAREN\n"); }
                      | REDUCE PLUS LPAREN expr RPAREN { printf("REDUCE PLUS LPAREN expr RPAREN\n"); }
                      | EXPAND LPAREN expr RPAREN { printf("EXPAND LPAREN expr RPAREN\n"); }
                      ;

bitslice:             ID { $$ = values[(string)$1];}
                      | NUMBER { $$ = Builder.getInt32($1);}
                      | bitslice_list { $$ = $1;}
                      | LPAREN expr RPAREN { $$ = $2;}
                      | bitslice NUMBER { $$ = getBit($1,Builder.getInt32($2));}
                      | bitslice DOT ID 
                      {
                        // Get value of ID from slices dictionary
                        Value* offset = slices[(string)$3];
                        // Get id bit from bitslice
                        $$ = getBit($1, offset);
                      }
// 566 only
                      | bitslice LBRACKET expr RBRACKET { printf("bitslice LBRACKET expr RBRACKET\n"); }
                      | bitslice LBRACKET expr COLON expr RBRACKET { printf("bitslice LBRACKET expr COLON expr RBRACKET\n"); }
                      ;

bitslice_list: LBRACE bitslice_list_helper RBRACE { $$ = $2;}
                      ;

bitslice_list_helper:  bitslice { $$ = getLowestBit($1); }
                      | bitslice_list_helper COMMA bitslice { $$ = do_leftshiftbyn_add($1,1,$3); }
;

bitslice_lhs:         ID { $$ = $1; }
                      | bitslice_lhs NUMBER { printf("bitslice_lhs NUMBER\n"); }
                      | bitslice_lhs DOT ID { printf("bitslice_lhs DOT ID\n"); }
// 566 only
                      | bitslice_lhs LBRACKET expr RBRACKET { printf("bitslice_lhs LBRACKET expr RBRACKET\n"); }
                      | bitslice_lhs LBRACKET expr COLON expr RBRACKET { printf("bitslice_lhs LBRACKET expr COLON expr RBRACKET\n"); }
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
