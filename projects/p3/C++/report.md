1. Describe your implementation of LICM in pseudocode including any bonus optimizations. 
    Also, give pertinent details about any statistical counters you collect and report.

LICM for Module M:
    For each Function F in M:
        guard F is not empty
        For each Loop in F:
            recursively iterate over all it's subloops, depth first, and run LICM over each Loop
    BONUS:  CSE
            DCE
            InstructionSimplification
            Eliminate redundant memory instructions over M

RecursiveLICM for Loop L:
    For each subloop in L:
        RecursiveLICM for subloop
    LICM for L

LICM for Loop L:
    guard there is a PreHeader
    guard there are more than 0 blocks in L (maybe unnecessary check)
    For each BasicBlock in L:
        For each Instruction I in BasicBlock:
            Scan the instruction and set analysis flags
            MoveLoopInvariantInstruction I

MoveLoopInvariantInstruction I:
    If I has LoopInvariantOperands:
        Loop.makeLoopInvariant(I)
    If I was made Loop Invariant:
        increment counter
    Else if ( I is a Load Instruction ):
        If ( Load Instruction can be moved out to Preheader):
            Move Load Instruction to Preheader
            increment counter
    Else if ( I is a Store Instruction ):
        If ( Store Instruction can be moved out to Exit Blocks):
        BONUS: Sink Store Instruction   
        BONUS: increment counter

Load Instruction can be moved out to Preheader:
    guard Instruction is not Volatile
    If there is another CallInst in Loop with side effects: return false

    if any store refers to an address that is not the result of an alloca or a global variable allocation, assume it may load/store to any address. 
        return false
    
    if no store to any unknown address and load address is loop invariant,
        return true
    

BONUS: Store Instruction can be moved out to Exit Blocks:
    similar logic as above but for stores

BONUS: Sink Store Instruction:
    For all the exit blocks in Loop:
        make a new block
        place block after exit block
        clone store to that block, insert it before the terminator
        update DominatorTree
    erase store instruction

BONUS: CSE Pass with DCE, Constant Propagation, redundant Load and Store elimination
    same as P2


2. How many instructions were moved using your implementation of Loop Invariant Code Motion?


3.  How many instructions were moved per loop on average?
    What happens to the average when you precede LICM with other optimizations, like mem-to-reg and CSE?

3. 566 only or 466 bonus: How many load instructions were moved versus non-load instructions?
