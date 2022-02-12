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

// ## DEBUGGING
void debug(Value* val, string message) {
  val->print(errs(), true);
  printf(" <- %s\n", message.c_str());
}

// ## TYPES

// A struct which holds Index and Range
struct Slice {
  Value* start;
  Value* range;
};

// A struct which holds values, index and range
struct ValueSlice {
  Value* value;
  Slice slice;
};


// ## GLOBAL VARIABLES

// Dictionary, holds all major values
unordered_map <string, ValueSlice> valueSliceDict;

// A Dictionary that holds Slice as value and string as keys
unordered_map <string, Slice> slicesDict;

// Bitslice IDs Helper
int bitsliceIDs = 0;

// ## FUNCTIONS

// Methods for ValueSlice

// 1. Add value in the mask bits
//

// Methods for SlicesDict

// Add Slice to the slicesDict dictionary
void addSlice(string key, Slice slice) {
  slicesDict.insert(pair<string, Slice>(key, slice));
}

// Methods for ValueSliceDict 

// Add a value to the ValueSlice dictionary
void addValueSlice(string name, Value* value, Value* start, Value* range) {
  ValueSlice vs;
  vs.value = value;
  Slice s;
  s.start = start;
  s.range = range;
  vs.slice = s;
  // Insert vs into valueSliceDict
  valueSliceDict.insert(make_pair(name, vs));
}

// Add a value to the ValueSlice dictionary
void addValueSlice(string name, Value* value, Slice slice) {
  ValueSlice vs;
  vs.value = value;
  vs.slice = slice;
  // Insert vs into valueSliceDict
  valueSliceDict.insert(make_pair(name, vs));
}

// Add Slice to a ValueSlice dictionary
void addSliceToValueSlice(string name, Slice slice) {
  Value* value = valueSliceDict[name].value;
  addValueSlice(name, value, slice);
}

void addNewValue(string name, Value* value) {
  ValueSlice vs;
  vs.value = value;
  Slice s;
  s.start = Builder.getInt32(0);
  s.range = Builder.getInt32(0xFFFFFFFF); // all 1s
  vs.slice = s;
  valueSliceDict.insert(make_pair(name, vs));
}

// Bit manipulation instructions

// Create a function to retrive a bit from llvm builder value
Value* getBit(Value* value, Value* position) {
  return Builder.CreateAnd(Builder.CreateLShr(value, position), Builder.getInt32(1));
}

// Create a function to retrive a bit from llvm builder value
Value* getBit(Value* value, int position) {
  return Builder.CreateAnd(Builder.CreateLShr(value, position), Builder.getInt32(1));
}

// Get lowest bit from integer
Value* getLowestBit(Value* value) {
  return Builder.CreateAnd(value, Builder.getInt32(1));
}

Value* do_leftshiftbyn_add(Value* value, Value* shift, Value* add) {
  return Builder.CreateAdd(Builder.CreateShl(value, shift), add);
}

Value* do_leftshiftbyn_add(Value* value, int shift, Value* add) {
  // Left shift $1 by 1, add $3 to it
  return do_leftshiftbyn_add(value, Builder.getInt32(shift), add);
}

Value* createMask(Value* start, Value* range) {
  // Make all bits 1 from start to start + range

  // start = 3, range = 2, N no. of bits
  // 0000 0000 0001 1000 : output
  // 1111 1111 1111 1111 >> start
  // 0001 1111 1111 1111 << N-range
  // 1100 0000 0000 0000 >> N - range - start = 12 -2 -3 = 7
  // 0000 0000 0001 1000

  // nMinusRange = N - range
  Value* nMinusRange = Builder.CreateSub(Builder.getInt32(32), range);

  Value* allOnes = Builder.getInt32(0xFFFFFFFF);
  Value* rightShifted = Builder.CreateLShr(allOnes, start);
  Value* leftShifted = Builder.CreateShl(rightShifted, nMinusRange);

  // nMinusRangeMinusStart = N - range - start
  Value* nMinusRangeMinusStart = Builder.CreateSub(nMinusRange, start);
  debug(start, "start");
  debug(range, "range");
  debug(nMinusRangeMinusStart, "nMinusRangeMinusStart");
  return nMinusRangeMinusStart;
}

Value* getMaskedValue(Value* value, Slice slice) {
  // Input: slice(start, range), value
  Value* mask = createMask(slice.start, slice.range);
  // Output: value && MASK, right shift range or (range-1)
  Value* valueAndMask = Builder.CreateAnd(mask, value);
  Value* valueAligned = Builder.CreateLShr(valueAndMask, slice.start);
  return valueAligned;
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
                          string arg_name = $2->at(arg_no);
                          // match name to position
                          addNewValue(arg_name, &a);
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

statements_opt:       %empty {}
                      | statements;

statements:           statement
                      | statements statement 
                      ;

statement:            bitslice_lhs ASSIGN expr ENDLINE 
                      { 
                        // This is more complex than it seems
                        addNewValue(string($1), $3);
                      }
                      | SLICE field_list ENDLINE
                      {
                        // reset the global variable here
                        bitsliceIDs = 0;
                      }
                      ;

field_list:           field_list COMMA field { printf("field_list COMMA field\n"); }
                      | field { printf("field\n"); }
                      ;

field:                ID COLON expr 
                      {
                        // a:4
                        // Make Slice struct with start=$3 and range=0, and store in slicesDict
                        cout << "Saved in slicesDict: Key " << $1 << endl;
                        debug($3, " <- Value");
                        // TODO helper function
                        Slice slice = Slice{$3, Builder.getInt32(0)};
                        addSlice(string($1), slice);
                      }
                      | ID LBRACKET expr RBRACKET COLON expr 
                      {
                        // a[4]:2
                        // Make new Slice, start = $6, range = $3
                        Slice slice = {$6, $3};
                        // Store the value in slices Dictionary
                        // TODO helper function
                        addSlice(string($1), slice);
                      }
// 566 only below
                      | ID 
                      {
                        // global variable to track the current position
                        Value* id = Builder.getInt32(bitsliceIDs);
                        // Make Slice with start and end as bitsliceIDs
                        // TODO helper function
                        addSlice(string($1), Slice{id, id});
                        // Increment bitsliceIDs
                        bitsliceIDs++;
                        // reset it at ENDLINE
                      }
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
                        // TODO: Check running secret tests with Unsigned and Signed versions of code
                      }
                      | expr MUL expr     {$$ = Builder.CreateMul($1, $3);}
                      | expr DIV expr     {$$ = Builder.CreateSDiv($1, $3);}
                      | expr MOD expr     {$$ = Builder.CreateSRem($1, $3);}
/* 566 only */
                      | REDUCE AND LPAREN expr RPAREN
                      {
                        // 111111 -> 1
                        // 101101 -> 0
                        // Divide by 111111 all 1s
                        // Divide expr by 0xFFFFFFFF
                        $$ = Builder.CreateUDiv($4, Builder.getInt32(0xFFFFFFFF));
                      }
                      | REDUCE OR LPAREN expr RPAREN 
                      {
                        // OR all the bits
                        // 11111 -> 1
                        // 00000 -> 0
                        // 100000 -> 1
                        
                        // If value greater than 1, return 1

                        // Check if value is greater than 1
                        Value* boolean = Builder.CreateICmpUGT($4, Builder.getInt32(0));

                        // Convert boolean to integer
                        $$ = Builder.CreateZExt(boolean, Builder.getInt32Ty());

                      }
                      | REDUCE XOR LPAREN expr RPAREN 
                      {
                        // 10000 -> 1
                        // 00000 -> 0
                        // 11111 -> 1
                        // 11110 -> 0
                        // XOR all the bitfields
                        // Add all the individual bits of expr
                        
                        Value* xor_temp = Builder.getInt32(0);

                        for (int i = 0; i < 32; i++)
                        {
                          // Get i bit from expr
                          Value* bit = getBit($4, i);
                          // Add the bit to xor
                          xor_temp = Builder.CreateXor(xor_temp, bit);
                        }
                        $$ = xor_temp;

                      }
                      | REDUCE PLUS LPAREN expr RPAREN 
                      {
                        // 00000 -> 0
                        // 11111 -> 5
                        // 01010 -> 2
                        // 10101 -> 3
                        
                        // Add all the individual bits of expr
                        Value* sum = Builder.getInt32(0);

                        for (int i = 0; i < 32; i++)
                        {
                          // Get i bit from expr
                          Value* bit = getBit($4, i);
                          // Add the bit to sum
                          sum = Builder.CreateAdd(sum, bit);
                        }
                        $$ = sum;
                      }
                      | EXPAND LPAREN expr RPAREN 
                      {                        
                        // get lowest bit of expr
                        Value* lowest_bit = getLowestBit($3);
                        
                        // Make a value of maximum unsigned 32 bits
                        Value* max_32 = Builder.getInt32(0xFFFFFFFF);

                        // Multiply lowest_bit and max_32, so it fills all the 32 bits
                        Value* result = Builder.CreateMul(lowest_bit, max_32);

                        $$ = result;
                      }
                      ;

bitslice:             ID { $$ = valueSliceDict[(string)$1].value; }
                      | NUMBER { $$ = Builder.getInt32($1);}
                      | bitslice_list { $$ = $1;}
                      | LPAREN expr RPAREN { $$ = $2;}
                      | bitslice NUMBER { $$ = getBit($1,Builder.getInt32($2));}
                      | bitslice DOT ID 
                      {
                        // From slicesDict, grab value of Slice using key ID
                        // Check if slicesDict has key ID
                        if (slicesDict.find(string($3)) != slicesDict.end())
                        {
                          Slice slice = slicesDict[string($3)];
                          debug(slice.start, " Slice start");
                          debug(slice.range, " Slice range");
                          
                          // Input: slice(start, range), bitslice
                          $$ = getMaskedValue($1, slice);
                        }
                        else { yyerror("Slice not found"); }
                      }
// 566 only
                      | bitslice LBRACKET expr RBRACKET { $$ = getBit($1,$3); }
                      | bitslice LBRACKET expr COLON expr RBRACKET { printf("bitslice LBRACKET expr COLON expr RBRACKET\n"); }
                      ;

bitslice_list:        LBRACE bitslice_list_helper RBRACE { $$ = $2;}
                      ;

bitslice_list_helper: bitslice { $$ = getLowestBit($1); }
                      | bitslice_list_helper COMMA bitslice { $$ = do_leftshiftbyn_add($1,1,getLowestBit($3)); }
;

bitslice_lhs:         ID { $$ = $1; }
                      | bitslice_lhs NUMBER { printf("bitslice_lhs NUMBER\n"); }
                      | bitslice_lhs DOT ID 
                      {
                        // Todo18
                        if (slicesDict.find(string($3)) != slicesDict.end())
                        {
                          // key: ValueSlice Dictionary
                          // ID: char, for Slice Dictionary
                          // Add the Slice to ValueSlice's slice
                          // TODO helper function
                          Slice slice = slicesDict[(string)$3];
                          debug(slice.start, " <- Slice start");
                          debug(slice.range, " <- Slice range");
                          
                          // check if $1 is in ValueSliceDict else throw exception
                          if (valueSliceDict.find(string($1)) != valueSliceDict.end())
                          {
                            updateSliceInValueSlice(string($1), slice);
                          }
                          else { yyerror("LHS value not found in ValueSlice dictionary"); }
                        }
                        else { YYERROR; }
                        $$ = $1;
                        
                      }
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
