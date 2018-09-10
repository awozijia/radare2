#include "mcore.h"

#include <r_anal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Instructions are aligned by 2 bytes (16 bits)
#define MCORE_INSTR_ALIGN (2)

// MCORE control registers
static const char* mcore_ctrl_registers[] = {
	"psr",  // Processor Status Register
	"vbr",  // Vector Base Register
	"epsr", // Shadow Exception PSR
	"fpsr", // Shadow Fast Interrupt PSR
	"epc",  // Shadow Exception Program Counter
	"fpc",  // Shadow Fast Interrupt PC
	"ss0",  // Supervisor Scratch Register 0-4
	"ss1",
	"ss2",
	"ss3",
	"ss4",
	"gcr",  // Global Control Register
	"gsr",  // Global Status Register
	"cpidr",
	"dcsr",
	"cwr",
	"cr16",
	"cfr",
	"ccr",
	"capr",
	"pacr",
	"prsr",
	"cr22",
	"cr23",
	"cr24",
	"cr25",
	"cr26",
	"cr27",
	"cr28",
	"cr29",
	"cr30",
	"cr31",
	"unknown"
};

/*
 * MCORE Register usage
 * r0  | Stack Pointer                          | Preserved
 * r1  | Scratch                                | Destroyed
 * r2  | Argument Word 1/Return Buffer Address  | Destroyed/Preserved
 * r3  | Argument Word 2                        | Destroyed
 * r4  | Argument Word 3                        | Destroyed
 * r5  | Argument Word 4                        | Destroyed
 * r6  | Argument Word 5                        | Destroyed
 * r7  | Argument Word 6                        | Destroyed
 * r8  | Local                                  | Preserved
 * r9  | Local                                  | Preserved
 * r10 | Local                                  | Preserved
 * r11 | Local                                  | Preserved
 * r12 | Local                                  | Preserved
 * r13 | Local                                  | Preserved
 * r14 | Local                                  | Preserved
 * r15 | Link/Scratch                           | (Return Address)
 */

// MCORE instruction set
// http://www.ece.ualberta.ca/~cmpe490/documents/motorola/MCORERM.pdf

#define INVALID_FIELD (0)

typedef struct mcore_mask {
	ut16 mask;
	ut16 shift;
	ut16 type;
} mcore_mask_t;

typedef struct mcore_ops {
	const char* name;
	ut16 cpu;
	ut16 mask;
	ut64 type;
	ut16 n_args;
	mcore_mask_t args[ARGS_SIZE];
} mcore_ops_t;

ut16 load_shift[4] = { 2, 0, 1, 0 };

#define MCORE_INSTRS 265
mcore_ops_t mcore_instructions[MCORE_INSTRS] = {
	{ "bkpt"    , MCORE_CPU_DFLT, 0b0000000000000000, R_ANAL_OP_TYPE_ILL  , 0, {{0},{0},{0},{0},{0}} },
	{ "sync"    , MCORE_CPU_DFLT, 0b0000000000000001, R_ANAL_OP_TYPE_SYNC , 0, {{0},{0},{0},{0},{0}} },
	{ "rte"     , MCORE_CPU_DFLT, 0b0000000000000010, R_ANAL_OP_TYPE_RET  , 0, {{0},{0},{0},{0},{0}} },
	{ "rfi"     , MCORE_CPU_DFLT, 0b0000000000000011, R_ANAL_OP_TYPE_RET  , 0, {{0},{0},{0},{0},{0}} },
	{ "stop"    , MCORE_CPU_DFLT, 0b0000000000000100, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	{ "wait"    , MCORE_CPU_DFLT, 0b0000000000000101, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	{ "doze"    , MCORE_CPU_DFLT, 0b0000000000000110, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	{ "idly4"   , MCORE_CPU_DFLT, 0b0000000000000111, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	// 0b00000000000010ii, trap #ii 
	{ "trap"    , MCORE_CPU_DFLT, 0b0000000000001011, R_ANAL_OP_TYPE_NULL , 1, {{ 0b11, 0, TYPE_IMM },{0},{0},{0},{0}} },
	// 0b0000000000001100, mvtc 510E
	{ "mvtc"    , MCORE_CPU_510E, 0b0000000000001100, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	// 0b0000000000001101, cprc cp
	{ "cprc"    , MCORE_CPU_DFLT, 0b0000000000001101, R_ANAL_OP_TYPE_NULL , 0, {{0},{0},{0},{0},{0}} },
	// 0b000000000000111x, --
	// 0b000000000001iiii, cpseti cp
	{ "cpseti"  , MCORE_CPU_DFLT, 0b0000000000011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_IMM },{0},{0},{0},{0}} },
	// 0b000000000010rrrr, mvc
	{ "mvc"     , MCORE_CPU_DFLT, 0b0000000000101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000000011rrrr, mvcv
	{ "mvcv"    , MCORE_CPU_DFLT, 0b0000000000111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000000100rrrr, ldq
	{ "ldq"     , MCORE_CPU_DFLT, 0b0000000001001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000000101rrrr, stq
	{ "stq"     , MCORE_CPU_DFLT, 0b0000000001011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000000110rrrr, ldm
	{ "ldm"     , MCORE_CPU_DFLT, 0b0000000001101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000000111rrrr, stm
	{ "stm"     , MCORE_CPU_DFLT, 0b0000000001111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001000rrrr, dect
	{ "dect"    , MCORE_CPU_DFLT, 0b0000000010001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001001rrrr, decf
	{ "decf"    , MCORE_CPU_DFLT, 0b0000000010011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001010rrrr, inct
	{ "inct"    , MCORE_CPU_DFLT, 0b0000000010101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001011rrrr, incf
	{ "incf"    , MCORE_CPU_DFLT, 0b0000000010111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001100rrrr, jmp
	{ "jmp"     , MCORE_CPU_DFLT, 0b0000000011001111, R_ANAL_OP_TYPE_CALL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001101rrrr, jsr
	{ "jsr"     , MCORE_CPU_DFLT, 0b0000000011011111, R_ANAL_OP_TYPE_RET  , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001110rrrr, ff1
	{ "ff1"     , MCORE_CPU_DFLT, 0b0000000011101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000001111rrrr, brev
	{ "brev"    , MCORE_CPU_DFLT, 0b0000000011111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000010000rrrr, xtrb3
	{ "xtrb3"   , MCORE_CPU_DFLT, 0b0000000100001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000010001rrrr, xtrb2
	{ "xtrb2"   , MCORE_CPU_DFLT, 0b0000000100011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000010010rrrr, xtrb1
	{ "xtrb1"   , MCORE_CPU_DFLT, 0b0000000100101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000010011rrrr, xtrb0
	{ "xtrb0"   , MCORE_CPU_DFLT, 0b0000000100111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000010100rrrr, zextb
	{ "zextb"   , MCORE_CPU_DFLT, 0b0000000101001111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b1111, 0, TYPE_REG },{0},{0},{0}} },
	// 0b000000010101rrrr, sextb
	{ "sextb"   , MCORE_CPU_DFLT, 0b0000000101011111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b1111, 0, TYPE_REG },{0},{0},{0}} },
	// 0b000000010110rrrr, zexth
	{ "zexth"   , MCORE_CPU_DFLT, 0b0000000101101111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b1111, 0, TYPE_REG },{0},{0},{0}} },
	// 0b000000010111rrrr, sexth
	{ "sexth"   , MCORE_CPU_DFLT, 0b0000000101111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b1111, 0, TYPE_REG },{0},{0},{0}} },
	// 0b000000011000rrrr, declt
	{ "declt"   , MCORE_CPU_DFLT, 0b0000000110001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011001rrrr, tstnbz
	{ "declt"   , MCORE_CPU_DFLT, 0b0000000110011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011010rrrr, decgt
	{ "decgt"   , MCORE_CPU_DFLT, 0b0000000110101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011011rrrr, decne
	{ "decne"   , MCORE_CPU_DFLT, 0b0000000110111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011100rrrr, clrt
	{ "clrt"    , MCORE_CPU_DFLT, 0b0000000111001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011101rrrr, clrf
	{ "clrf"    , MCORE_CPU_DFLT, 0b0000000111011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011110rrrr, abs
	{ "abs"     , MCORE_CPU_DFLT, 0b0000000111101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b000000011111rrrr, not
	{ "not"     , MCORE_CPU_DFLT, 0b0000000111111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b00000010ssssrrrr, movt
	{ "movt"    , MCORE_CPU_DFLT, 0b0000001011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00000011ssssrrrr, mult
	{ "mult"    , MCORE_CPU_DFLT, 0b0000001111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00000101ssssrrrr, subu
	{ "subu"    , MCORE_CPU_DFLT, 0b0000010111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00000110ssssrrrr, addc
	{ "addc"    , MCORE_CPU_DFLT, 0b0000011011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00000111ssssrrrr, subc
	{ "subc"    , MCORE_CPU_DFLT, 0b0000011111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b0000100sssssrrrr, cprgr cp
	{ "cprgr"   , MCORE_CPU_DFLT, 0b0000100111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001010ssssrrrr, movf
	{ "movf"    , MCORE_CPU_DFLT, 0b0000101011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001011ssssrrrr, lsr
	{ "lsr"     , MCORE_CPU_DFLT, 0b0000101111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001100ssssrrrr, cmphs
	{ "cmphs"   , MCORE_CPU_DFLT, 0b0000101111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001101ssssrrrr, cmplt
	{ "cmplt"   , MCORE_CPU_DFLT, 0b0000110111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001110ssssrrrr, tst
	{ "tst"     , MCORE_CPU_DFLT, 0b0000111011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00001111ssssrrrr, cmpne
	{ "cmpne"   , MCORE_CPU_DFLT, 0b0000111111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b0001000cccccrrrr, mfcr
	{ "mfcr"    , MCORE_CPU_DFLT, 0b0001000111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_CTRL},{0},{0},{0}} },
	// 0b0001000111110bbb, psrclr
	{ "psrclr"  , MCORE_CPU_DFLT, 0b0001000111110111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b0111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b0001000111111bbb, psrset
	{ "psrset"  , MCORE_CPU_DFLT, 0b0001000111111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b0111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b00010010ssssrrrr, mov
	{ "mov"     , MCORE_CPU_DFLT, 0b0001001011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00010011ssssrrrr, bgenr
	{ "bgenr"   , MCORE_CPU_DFLT, 0b0001001111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00010100ssssrrrr, rsub
	{ "rsub"    , MCORE_CPU_DFLT, 0b0001010011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00010101ssssrrrr, lxw
	{ "lxw"     , MCORE_CPU_DFLT, 0b0001010111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00010110ssssrrrr, and
	{ "and"     , MCORE_CPU_DFLT, 0b0001011011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00010111ssssrrrr, xor
	{ "xor"     , MCORE_CPU_DFLT, 0b0001011111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b0001100cccccrrrr, mtcr
	{ "mtcr"    , MCORE_CPU_DFLT, 0b0001000111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_CTRL},{0},{0},{0}} },
	// 0b00011010ssssrrrr, asr
	{ "asr"     , MCORE_CPU_DFLT, 0b0001101011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00011011ssssrrrr, lsl
	{ "lsl"     , MCORE_CPU_DFLT, 0b0001101111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00011100ssssrrrr, addu
	{ "addu"    , MCORE_CPU_DFLT, 0b0001110011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00011101ssssrrrr, lxh
	{ "lxh"     , MCORE_CPU_DFLT, 0b0001110111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00011110ssssrrrr, or
	{ "or"      , MCORE_CPU_DFLT, 0b0001111011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b00011111ssssrrrr, andn
	{ "andn"    , MCORE_CPU_DFLT, 0b0001111111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_REG },{0},{0},{0}} },
	// 0b0010000iiiiirrrr, addi
	{ "addi"    , MCORE_CPU_DFLT, 0b0010000111111111, R_ANAL_OP_TYPE_NULL , 3, {{ 0b1111, 0, TYPE_REG },{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0}} },
	// 0b0010001iiiiirrrr, cmplti
	{ "cmplti"  , MCORE_CPU_DFLT, 0b0010001111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0010010iiiiirrrr, subi
	{ "subi"    , MCORE_CPU_DFLT, 0b0010010111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0010011sssssrrrr, cpwgr cp
	{ "cpwgr"   , MCORE_CPU_DFLT, 0b0010011111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_CTRL},{0},{0},{0}} },
	// 0b0010100iiiiirrrr, rsubi
	{ "rsubi"   , MCORE_CPU_DFLT, 0b0010100111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0010101iiiiirrrr, cmpnei
	{ "cmpnei"  , MCORE_CPU_DFLT, 0b0010101111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001011000000rrrr, bmaski #32(set)
	{ "bmaski"  , MCORE_CPU_DFLT, 0b0010110000001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000001rrrr, divu
	{ "divu"    , MCORE_CPU_DFLT, 0b0010110000011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000010rrrr, mflos 610E
	{ "mflos"   , MCORE_CPU_610E, 0b0010110000101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000011rrrr, mfhis 610E
	{ "mfhis"   , MCORE_CPU_610E, 0b0010110000101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000100rrrr, mtlo 620
	{ "mtlo"    , MCORE_CPU_620 , 0b0010110001001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000101rrrr, mthi 620
	{ "mthi"    , MCORE_CPU_620 , 0b0010110001011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000110rrrr, mflo 620
	{ "mtlo"    , MCORE_CPU_620 , 0b0010110001101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011000111rrrr, mfhi 620
	{ "mthi"    , MCORE_CPU_620 , 0b0010110001111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001011001iiirrrr, bmaski
	{ "bmaski"  , MCORE_CPU_DFLT, 0b0010110011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b001110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b00101101iiiirrrr, bmaski
	{ "bmaski"  , MCORE_CPU_DFLT, 0b0010110111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0010111iiiiirrrr, andi
	{ "andi"    , MCORE_CPU_DFLT, 0b0010111111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0011000iiiiirrrr, bclri
	{ "bclri"   , MCORE_CPU_DFLT, 0b0011000111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001100100000rrrr, cpwir cp
	{ "cpwir"   , MCORE_CPU_DFLT, 0b0011001000001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001100100001rrrr, divs 
	{ "divs"    , MCORE_CPU_DFLT, 0b0011001000011111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001100100010rrrr, cprsr cp
	{ "cprsr"   , MCORE_CPU_DFLT, 0b0011001000101111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001100100011rrrr, cpwsr cp
	{ "cpwsr"   , MCORE_CPU_DFLT, 0b0011001000111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001100100100rrrr, --
	// 0b001100100101rrrr, --
	// 0b001100100110rrrr, --
	// 0b001100100111rrrr, bgeni
	{ "bgeni"   , MCORE_CPU_DFLT, 0b0011001001111111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b001100101iiirrrr, bgeni
	{ "bgeni"   , MCORE_CPU_DFLT, 0b0011001011111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b001110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b00110011iiiirrrr, bgeni
	{ "bgeni"   , MCORE_CPU_DFLT, 0b0011001111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b011110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0011010iiiiirrrr, bseti
	{ "bgeni"   , MCORE_CPU_DFLT, 0b0011010111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b0011011iiiiirrrr, btsti
	{ "btsti"   , MCORE_CPU_DFLT, 0b0011011111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001110000000rrrr, xsr
	{ "xsr"     , MCORE_CPU_DFLT, 0b0011100000001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b0011100iiiiirrrr, rotli
	{ "rotli"   , MCORE_CPU_DFLT, 0b0011100111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001110100000rrrr, asrc
	{ "asrc"    , MCORE_CPU_DFLT, 0b0011101000001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b0011101iiiiirrrr, asri
	{ "asri"    , MCORE_CPU_DFLT, 0b0011101111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001111000000rrrr, lslc
	{ "lslc"    , MCORE_CPU_DFLT, 0b0011110000001111, R_ANAL_OP_TYPE_NULL , 1, {{ 0b1111, 0, TYPE_REG },{0},{0},{0},{0}} },
	// 0b0011110iiiiirrrr, lsli
	{ "lsli"    , MCORE_CPU_DFLT, 0b0011110111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b001111100000rrrr, lsrc
	// 0b0011111iiiiirrrr, lsri
	// 0b01000000ssssrrrr, omflip0 620
	// 0b01000001ssssrrrr, omflip1 620
	// 0b01000010ssssrrrr, omflip2 620
	// 0b01000011ssssrrrr, omflip3 620
	// 0b010001xxssssrrrr, --
	// 0b01001xxxssssrrrr, --
	// 0b01010000ssssrrrr, muls  610E
	// 0b01010001ssssrrrr, mulsa 610E
	// 0b
	// 0b01010010ssssrrrr, mulss 610E
	// 0b01010011ssssrrrr, --
	// 0b01010100ssssrrrr, mulu 610E
	// 0b01010101ssssrrrr, mulua 610E
	// 0b01010110ssssrrrr, mulus 610E
	// 0b01010111ssssrrrr, --
	// 0b01011000ssssrrrr, vmulsh 610E
	// 0b01011001ssssrrrr, vmulsha 610E
	// 0b01011010ssssrrrr, vmulshs 610E
	// 0b01011011ssssrrrr, --
	// 0b01011100ssssrrrr, vmulsw 610E
	// 0b01011101ssssrrrr, vmulswa 610E
	// 0b01011110ssssrrrr, vmulsws 610E
	// 0b01011111ssssrrrr, --
	// 0b01100iiiiiiirrrr, movi
	{ "movi"    , MCORE_CPU_DFLT, 0b0110011111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b1111, 0, TYPE_REG },{ 0b11111110000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b01101000ssssrrrr, mulsh
	// 0b01101001ssssrrrr, mulsha 610E
	// 0b01101010ssssrrrr, mulshs 610E
	// 0b01101011sssssrrr, cprcr cp
	// 0b01101100ssssrrrr, mulsw 610E
	// 0b01101101ssssrrrr, mulswa 610E
	// 0b01101110ssssrrrr, mulsws 610E
	// 0b01101111sssssrrr, cpwcr cp
	{ "cpwcr"   , MCORE_CPU_DFLT, 0b0110111111111111, R_ANAL_OP_TYPE_NULL , 2, {{ 0b111, 0, TYPE_REG },{ 0b011111000, 4, TYPE_IMM },{0},{0},{0}} },
	// 0b01110000dddddddd, jmpi
	{ "jmpi"    , MCORE_CPU_DFLT, 0b0111000011111111, R_ANAL_OP_TYPE_JMP  , 2, {{ 0b11111111, 0, TYPE_JMP },{0},{0},{0},{0}} },
	// 0b0111zzzzdddddddd, lrw
	{ "lrw"     , MCORE_CPU_DFLT, 0b0111111111111111, R_ANAL_OP_TYPE_LOAD , 2, {{ 0b11111111, 0, TYPE_MEM },{ 0b111100000000, 8, TYPE_REG },{0},{0},{0}} },
	// 0b01111111dddddddd, jsri
	{ "jsri"    , MCORE_CPU_DFLT, 0b0111111111111111, R_ANAL_OP_TYPE_CALL , 1, {{ 0b11111111, 0, TYPE_JMPI},{0},{0},{0},{0}} },
	// 0b1000zzzziiiirrrr, ld.w
	{ "ld.w"    , MCORE_CPU_DFLT, 0b1000111111111111, R_ANAL_OP_TYPE_LOAD , 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b1001zzzziiiirrrr, st.w
	{ "st.w"    , MCORE_CPU_DFLT, 0b1001111111111111, R_ANAL_OP_TYPE_STORE, 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b1010zzzziiiirrrr, ld.b
	{ "ld.b"    , MCORE_CPU_DFLT, 0b1010111111111111, R_ANAL_OP_TYPE_LOAD , 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b1011zzzziiiirrrr, st.b
	{ "st.b"    , MCORE_CPU_DFLT, 0b1011111111111111, R_ANAL_OP_TYPE_STORE, 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b1100zzzziiiirrrr, ld.h
	{ "ld.h"    , MCORE_CPU_DFLT, 0b1100111111111111, R_ANAL_OP_TYPE_LOAD , 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b1101zzzziiiirrrr, st.h
	{ "st.h"    , MCORE_CPU_DFLT, 0b1101111111111111, R_ANAL_OP_TYPE_STORE, 4, {{ 0b1111, 0, TYPE_NONE},{ 0b11110000, 4, TYPE_NONE},{ 0b111100000000, 8, TYPE_NONE},{ 0b110000000000000, 13, TYPE_NONE},{0}} },
	// 0b11100ddddddddddd, bt
	{ "bt"      , MCORE_CPU_DFLT, 0b1110011111111111, R_ANAL_OP_TYPE_CJMP , 1, {{ 0b11111111111, 0, TYPE_JMP},{0},{0},{0},{0}} },
	// 0b11101ddddddddddd, bf
	{ "bf"      , MCORE_CPU_DFLT, 0b1110111111111111, R_ANAL_OP_TYPE_CJMP , 1, {{ 0b11111111111, 0, TYPE_JMP},{0},{0},{0},{0}} },
	// 0b11110ddddddddddd, br
	{ "br"      , MCORE_CPU_DFLT, 0b1111011111111111, R_ANAL_OP_TYPE_CJMP , 1, {{ 0b11111111111, 0, TYPE_JMP},{0},{0},{0},{0}} },
	// 0b11111ddddddddddd, bsr
	{ "bsr"     , MCORE_CPU_DFLT, 0b1111111111111111, R_ANAL_OP_TYPE_CALL , 1, {{ 0b11111111111, 0, TYPE_JMP},{0},{0},{0},{0}} },
};

static mcore_t *find_instruction(const ut8* buffer) {
	ut32 i = 0;
	mcore_ops_t *op_ptr = NULL;
	mcore_t *op = NULL;
	if (!buffer || !(op = malloc (sizeof (mcore_t)))) {
		return NULL;
	}
	memset (op, 0, sizeof (mcore_t));
	ut32 count = sizeof (mcore_instructions) / sizeof (mcore_ops_t);
	ut16 data = buffer[1] << 8;
	data |= buffer[0];
	op->bytes = data;
	op->size = MCORE_INSTR_ALIGN;
	if (data == 0) {
		op_ptr = &mcore_instructions[0];
	} else {
		for (i = 1; i < count; i++) {
			op_ptr = &mcore_instructions[i];
			ut16 masked = data & op_ptr->mask;
			// always masking with zero returns 0
			if (masked == data) {
				break;
			}
		}
		if (i >= count) {
			op->name = "illegal";
			return op;
		}
	}

	if (!strncmp (op_ptr->name, "lrw", 3) && (data & 0b111100000000) == 0b111100000000) {
		// is jump
		if (i > 0 && i< MCORE_INSTRS) {
			op_ptr = &mcore_instructions[i + 1];
		}
	}
	if (!op_ptr) {
		return NULL;
	}

	op->type = op_ptr->type;
	op->name = op_ptr->name;
	op->n_args = op_ptr->n_args;
	for (i = 0; i < op_ptr->n_args; ++i) {
		op->args[i].value = (data & op_ptr->args[i].mask) >> op_ptr->args[i].shift;
		op->args[i].type = op_ptr->args[i].type;
	}
	return op;
}

int mcore_init(mcore_handle* handle, const ut8* buffer, const ut32 size) {
	if (!handle || !buffer || size < 2) {
		return 1;
	}
	handle->pos = buffer;
	handle->end = buffer + size;
	return 0;
}

mcore_t* mcore_next(mcore_handle* handle) {
	mcore_t *op = NULL;
	if (!handle || handle->pos + MCORE_INSTR_ALIGN > handle->end) {
		return NULL;
	}

	if (!op && handle->pos + 2 <= handle->end) {
		op = find_instruction (handle->pos);
	}
	handle->pos += MCORE_INSTR_ALIGN;
	
	return op;
}

void mcore_free(mcore_t* instr) {
	free (instr);
}

void print_loop(char* str, int size, ut64 addr, mcore_t* instr) {
	ut32 i;
	int bufsize = size;
	int add = snprintf (str, bufsize, "%s", instr->name);
	for (i = 0; add > 0 && i < instr->n_args && add < bufsize; ++i) {
		if (instr->args[i].type == TYPE_REG) {
			add += snprintf (str + add, bufsize - add, " r%u,", instr->args[i].value);
		} else if (instr->args[i].type == TYPE_IMM) {
			add += snprintf (str + add, bufsize - add, " 0x%x,", instr->args[i].value);
		} else if (instr->args[i].type == TYPE_MEM) {
			add += snprintf (str + add, bufsize - add, " 0x%x(r%d),",
				instr->args[i + 1].value, instr->args[i].value);
			i++;
		} else if (instr->args[i].type == TYPE_JMPI) {
			ut64 jump = addr + ((instr->args[i].value << 2) & 0xfffffffc);
			add += snprintf (str + add, bufsize - add, " [0x%" PFMT64x"],", jump);
		} else if (instr->args[i].type == TYPE_JMP) {
			ut64 jump = addr + instr->args[i].value + 1;
			add += snprintf (str + add, bufsize - add, " 0x%" PFMT64x ",", jump);
		} else if (instr->args[i].type == TYPE_CTRL) {
			ut32 pos = instr->args[i].value;
			if (pos >= 32) {
				pos = 32;
			}
			add += snprintf (str + add, bufsize - add, " %s,", mcore_ctrl_registers[pos]);
		}
	}
	if (instr->n_args) {
		// removing a comma
		*(str + add - 1) = 0;
	}
}

void mcore_snprint(char* str, int size, ut64 addr, mcore_t* instr) {
	ut32 imm;
	if (!instr || !str) {
		return;
	}
	switch (instr->type) {
	case R_ANAL_OP_TYPE_LOAD:
	case R_ANAL_OP_TYPE_STORE:
		imm = instr->args[1].value << load_shift[instr->args[3].value];
		snprintf (str, size, "%s r%u, (r%u, 0x%x)",
			instr->name, instr->args[2].value, instr->args[0].value, imm);
		break;
	default:
		print_loop (str, size, addr, instr);
		break;
	}
}
