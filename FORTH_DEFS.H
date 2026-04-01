#ifndef _FORTH_DEFS_H_
#define _FORTH_DEFS_H_

#define FORTH_STACK_DEPTH 32
#define FORTH_TOS_INIT 0xDEADBEEF
#define FORTH_RAM_SIZE (4 * 1024)
#define FORTH_CONTEXT_SIZE 8
#define FORTH_NLEN_MASK 0x1F

#define F_NOOP 0X0
#define F_HALT 0X1
#define F_SWLIT 0X2
#define F_UWLIT 0X3
#define F_ULLIT 0X4
#define F_SWDOCONST 0X5
#define F_UWDOCONST 0X6
#define F_ULDOCONST 0X7

#define F_BRANCH 0X8
#define F_ZBRANCH 0X9
#define F_EXIT 0XA
#define F_CALL 0XB
#define F_EXECUTE 0XC
#define F_PERFORM 0XD
#define F_EQCHK 0XE
#define F_PZCHK 0XF

#define F_DROP 0X10
#define F_DUP 0X11
#define F_SWAP 0X12
#define F_OVER 0X13
#define F_ROT 0X14
#define F_NROT 0x15
#define F_NIP 0x16
#define F_TOR 0x17

#define F_2DROP 0X18
#define F_2DUP 0X19
#define F_2SWAP 0X1A
#define F_2OVER 0X1B
#define F_FROMR 0X1F

#define F_PLUS 0X20
#define F_1PLUS 0X21
#define F_2PLUS 0X22
#define F_4PLUS 0X23
#define F_MINUS 0X24
#define F_1MINUS 0X25
#define F_2MINUS 0X26
#define F_4MINUS 0X27

#define F_MULTI 0X28
#define F_2MULTI 0X29
#define F_4MULTI 0X2A
#define F_8MULTI 0X2B
#define F_DIVID 0X2C
#define F_2DIVID 0X2D
#define F_4DIVID 0X2E
#define F_8DIVID 0X2F

#define F_LSHIFT 0X30
#define F_RSHIFT 0X31
#define F_AND 0X32
#define F_OR 0X33
#define F_XOR 0X34
#define F_INVERT 0X35
#define F_NEGATE 0X36
#define F_BIC 0X37

#define F_EQ 0X38
#define F_NE 0X39
#define F_LT 0X3A
#define F_GT 0X3B
#define F_EQZ 0X3C
#define F_NEZ 0X3D
#define F_ULT 0X3E
#define F_UGT 0X3F

#define F_CLOAD 0X40
#define F_WLOAD 0X41
#define F_LLOAD 0X42

#define F_CSTORE 0X44
#define F_WSTORE 0X45
#define F_LSTORE 0X46

#define F_BLANK 0X48
#define F_FILL 0X49
#define F_CMOVE 0X4A
#define F_COMPARE 0X4B

#define F_EARLY_EMIT 0x4C
#define F_EARLY_KEY 0x4D
#define F_USER_EMIT 0X4E
#define F_USER_KEY 0x4F

#define F_DBGON 0X50
#define F_DBGOFF 0x51
#define F_NUM2HEX 0x52

#define F_USER_DOT 0x53

#define F_DEPTH 0x54
#define F_PICK 0x55

#define F_ISNUMBER 0x56
#define F_TONUMBER 0x57

#define F_WALIGNED 0x58
#define F_LALIGNED 0x59
#define F_XALIGNED 0x5A

#define F_TONLEN 0x5B
#define F_TONAME 0x5C
#define F_TOPREV 0X5D
#define F_CONTEXT 0x5E
#define F_CONTEXT_SIZE 0x5F

#define F_WORDLIST_SEARCH 0x60
#define F_ISDELIM 0x61
#define F_TIB 0x62
#define F_TINLOAD 0x63
#define F_TINSTORE 0x64

#define F_STATELOAD 0X65
#define F_COMPOFF 0X66
#define F_COMPON 0x67
#define F_ISIMMEDIATE 0x68
#define F_PSTACK_RST 0x69

#define F_ABS 0X6A
#define F_XT2ADDR 0X6B
#define F_XT2ENTR 0X6C
#define F_BLENLOAD 0X6D
#define F_BLENSTORE 0X6E

#define F_ACM0_KEY 0X70
#define F_ACM0_EMIT 0x71
#define F_ACM1_KEY 0X72
#define F_ACM1_EMIT 0x73

#define F_SW_RESET 0X74
#define F_PW_RESET 0x75

#endif
