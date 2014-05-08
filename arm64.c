#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include "dat.h"
#include "fns.h"

enum {
	fI = 1<<25,
	fP = 1<<24,
	fLi = 1<<24,
	fU = 1<<23,
	fB = 1<<22,
	fW = 1<<21,
	fL = 1<<20,
	fS = 1<<20,
	fSg = 1<<6,
	fH = 1<<5,
};

void
invalid(u32int instr)
{
	suicide("undefined instruction %8ux @ %16lux", instr, P->PC - 4);
}

u32int
evenaddr(u32int addr, u32int mask)
{
	if((addr & mask) == 0)
		return addr;
	suicide("unaligned access %16lux @ %16lux", addr, P->PC - 4);
	return addr & ~mask;
}

static u32int
doshift(u32int instr)
{
	ulong amount, val;
	
	if((instr & (1<<4)) && (instr & (1<<7)))
		invalid(instr);
	
	if(instr & (1<<4))
		amount = P->R[(instr >> 8) & 15];
	else
		amount = (instr >> 7) & 31;
	val = P->R[instr & 15];
	switch((instr >> 5) & 3) {
	case 0:
		return val << amount;
	case 1:
		return val >> amount;
	case 2:
		return ((long) val) >> amount;
	case 3:
		return (val >> amount) | (val << (32 - amount));
	}
	return 0;
}

static void
single(u32int instr)
{
	long offset;
	u32int addr;
	u32int *Rn, *Rd;
	void *targ;
	Segment *seg;
	
	if(instr & fI) {
		if(instr & (1<<4))
			invalid(instr);
		offset = doshift(instr);
	} else
		offset = instr & ((1<<12) - 1);
	if(!(instr & fU))
		offset = - offset;
	Rn = P->R + ((instr >> 16) & 15);
	Rd = P->R + ((instr >> 12) & 15);
	if((instr & (fW | fP)) == fW)
		invalid(instr);
	if(Rn == P->R + 15) {
		if(instr & fW)
			invalid(instr);
		addr = P->PC + 4;
	}
	else
		addr = *Rn;
	if(instr & fP)
		addr += offset;
	if((instr & fB) == 0)
		addr = evenaddr(addr, 3);
	targ = vaddr(addr, 4, &seg);
	switch(instr & (fB | fL)) {
	case 0:
		*(u32int*) targ = *Rd;
		break;
	case fB:
		*(u8int*) targ = *Rd;
		break;
	case fL:
		*Rd = *(u32int*) targ;
		break;
	case fB | fL:
		*Rd = *(u8int*) targ;
		break;
	}
	if(Rd == P->R + 15 && !(instr & fL)) {
		if(instr & fB)
			*(u8int*) targ += 8;
		else
			*(u32int*) targ += 8;
	}
	segunlock(seg);
	if(!(instr & fP))
		addr += offset;
	if((instr & fW) || !(instr & fP))
		*Rn = addr;
}

static void
swap(u32int instr)
{
	u32int *Rm, *Rn, *Rd, *targ, addr, tmp;
	Segment *seg;
	
	Rm = P->R + (instr & 15);
	Rd = P->R + ((instr >> 12) & 15);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rm == P->R + 15 || Rd == P->R + 15 || Rn == P->R + 15)
		invalid(instr);
	addr = *Rn;
	if((instr & fB) == 0)
		addr = evenaddr(addr, 3);
	targ = (u32int *) vaddr(addr, 4, &seg);
	lock(&seg->lock);
	if(instr & fB) {
		tmp = *(u8int*) targ;
		*(u8int*) targ = *Rm;
		*Rd = tmp;
	} else {
		tmp = *targ;
		*targ = *Rm;
		*Rd = tmp;
	}
	unlock(&seg->lock);
	segunlock(seg);
}

static u64int
uadd64(u64int a, u64int b, u32int type, u32int *carry)
{
	uint64 r;

	if(type) {	// SUB
		r = a - b;
		if (r > a)
			*carry = 1;
		else
			*carry = 0;
	} else {	// ADD
		r = a + b;
		if(r < a || r < b)
			*carry = 1;
		else
			*carry = 0;
	}
	return r;
}

static vlong
add64(vlong a, vlong b, vlong type, vlong *overflow)
{
	vlong r;

	if(type) {	// SUB
		r = a - b;
		if((a & (1 << 63)) != (b & (1 << 63)) &&
		   (r & (1 << 63)) != (a & (1 << 63)))
			*overflow = 1;
		else
			*overflow = 0;
	} else {	// ADD
		r = a + b;
		if((a & (1 << 63)) == (b & (1 << 63)) &&
		   (r & (1 << 63)) != (a & (1 << 63)))
			*overflow = 1;
		else
			*overflow = 0;
	}
	return r;
}

static u32int
uadd32(u32int a, u32int b, u32int type, u32int *carry)
{
	uint64 r;

	if(type) {	// SUB
		r = a - b;
		if (r > a)
			*carry = 1;
		else
			*carry = 0;
	} else {	// ADD
		r = a + b;
		if(r < a || r < b)
			*carry = 1;
		else
			*carry = 0;
	}
	return r;
}

static long
add32(long a, long b, u32int type, u32int *overflow)
{
	long r;

	if(type) {	// SUB
		r = a - b;
		if((a & (1 << 31)) != (b & (1 << 31)) &&
		   (r & (1 << 31)) != (a & (1 << 31)))
			*overflow = 1;
		else
			*overflow = 0;
	} else {	// ADD
		r = a + b;
		if((a & (1 << 31)) == (b & (1 << 31)) &&
		   (r & (1 << 31)) != (a & (1 << 31)))
			*overflow = 1;
		else
			*overflow = 0;
	}
	return r;
}

static void
alu(u32int instr)
{
	u32int Rn, *Rd, operand, shift, result, op;
	u8int carry, overflow;
	
	Rn = P->R[(instr >> 16) & 15];
	Rd = P->R + ((instr >> 12) & 15);
	if(((instr >> 16) & 15) == 15) {
		Rn += 4;
		if(!(instr & fI) && (instr & (1<<4)))
			Rn += 4;
	}
	if(Rd == P->R + 15 && (instr & fS))
		invalid(instr);
	if(instr & fI) {
		operand = instr & 0xFF;
		shift = ((instr >> 8) & 15) << 1;
		operand = (operand >> shift) | (operand << (32 - shift));
	} else
		operand = doshift(instr);
	op = (instr >> 21) & 15;
	carry = 0;
	if(op >= 8 && op <= 11 && !(instr & fS))
		sysfatal("no PSR transfers plz");
	if(op >= 5 && op < 8) {
		if(P->CPSR & flC)
			carry = 1;
	} else {
		if(op != 4 && op != 5 && op != 11)
			carry = 1;
	}
	overflow = 0;
	switch(op) {
	case 0: case 8: result = Rn & operand; break;
	case 1: case 9: result = Rn ^ operand; break;
	case 2: case 6: case 10: result = add(Rn, operand, 1, &carry, &overflow); break;
	case 3: case 7: result = add(operand, Rn, 1, &carry, &overflow); break;
	case 4: case 5: case 11: result = add(operand, Rn, 0, &carry, &overflow); break;
	case 12: result = Rn | operand; break;
	case 13: result = operand; break;
	case 14: result = Rn & ~operand; break;
	case 15: result = ~operand; break;
	default: result = 0; /* never happens */
	}
	if(instr & fS) {
		P->CPSR &= ~FLAGS;
		if(result == 0)
			P->CPSR |= flZ;
		if(result & (1<<31))
			P->CPSR |= flN;
		if(carry && op > 1 && op < 12)
			P->CPSR |= flC;
		if(overflow)
			P->CPSR |= flV;
	}
	if(op < 8 || op >= 12)
		*Rd = result;
}

static int
cond(u32int instr)
{
	int r;

	switch((instr & 0xF) >> 1) {
	case 0:
		r = P->Z;
		break;
	case 1:
		r = P->C;
		break;
	case 2:
		r = P->N;
		break;
	case 3:
		r = P->V;
		break;
	case 4:
		r = P->C && !P->Z;
		break;
	case 5:
		r = P->N == P->V;
		break;
	case 6:
		r = P->N == P->V && !P->Z;
		break;
	case 7:
		r = 1;
		break;
	}
	if((instr & 0x1) && ((instr & 0xF) != 0xF))
		r = !r;
	return r;
}

static void
branch(u32int instr)
{
	u32int R, op;
	u32int b5, b31, b40, bit;
	u32int imm14, imm19, imm26;
	vlong offset, val;

	offset = 0;
	b31 = instr >> 31;
	op = instr << 7 >> 31;
	imm19 = instr << 8 >> 17;
	R = instr & 0x1F;
	val = b31 ? P->R[R] : (u32int)P->R[R];

	if((instr & 0x7E000000) == 0x34000000) {	// CBZ, CBNZ
		if(val==0 && op==0 || val!=0 && op!=0)
			offset = imm19 << 2;
	} else if((instr & 0xFF000010) == 0x54000000) {	// B.cond
		if(cond(instr))
			offset = imm19 << 2;
	} else if((instr & 0x7E000000) == 0x36000000) {	// TBZ, TBNZ
		b40 = instr << 8 >> 27;
		imm14 = instr << 13 >> 18;
		bit = (b31 << 5) + b40;
		if((val & 1<<bit) == 0 && op==0 || (val & 1<<bit) != 0 && op!=0)
			offset = imm14;
	} else if((instr & 0x7C000000) == 0x14000000) {	// B, BL
		imm26 = instr << 6 >> 6;
		if(b31)
			P->R[31] = P->PC;
		offset = imm26 << 2;
	} else if((instr & 0xFE000000) == 0xD6000000) {	// BL, BLR, RET
		R = instr << 22 >> 27;
		op = instr << 9 >> 30;
		if(op == 0x1)
			P->R[31] = P->PC;
		P->PC = P->R[R];
	} else {
		invalid(instr);
	}
	if(offset)
		P->PC += offset - 4;
}

static void
flags(vlong v)
{
	P->N = P->Z = P->C = P->V;
	if(v == 0)
		P->Z = 1;
	if(v < 0)
		P->N = 1;
	if(v >0)
}

static void
alui(u32int instr)
{
	u32int sf, op, shift, S, Rm, Rn, Rd;
	u32int imm12, val;

	sf = instr >> 31;
	op = instr << 1 >> 31;
	S = instr << 2 >> 31;
	shift = instr << 8 >> 30;
	Rn = (instr & 0x3E0) >> 5;
	Rd = instr & 0x1F;
	imm12 = instr << 10 >> 20;

	if((addr & 0x1F000000) == 0x11000000) {	// ADD, ADDS, SUB, SUBS
		val = sf ? R->R[Rn] : u32int(R->R[Rn]);
		P->R[Rd] = val + (op ? (imm12 << 12*shift) : -(imm12 << 12*shift));
		if(S)
			flags(P->R[Rd]);
	} else if {
		
	} else {
		invalid(instr);
	}
}

static void
halfword(u32int instr)
{
	u32int offset, target, *Rn, *Rd;
	Segment *seg;
	
	if(instr & (1<<22)) {
		offset = (instr & 15) | ((instr >> 4) & 0xF0);
	} else {
		if((instr & 15) == 15)
			invalid(instr);
		offset = P->R[instr & 15];
	}
	if(!(instr & fU))
		offset = - offset;
	if(!(instr & fP) && (instr & fW))
		invalid(instr);
	Rn = P->R + ((instr >> 16) & 15);
	Rd = P->R + ((instr >> 12) & 15);
	if(Rn == P->R + 15 || Rd == P->R + 15)
		sysfatal("R15 in halfword");
	target = *Rn;
	if(instr & fP)
		target += offset;
	if(instr & fH)
		target = evenaddr(target, 1);
	switch(instr & (fSg | fH | fL)) {
	case fSg: *(u8int*) vaddr(target, 1, &seg) = *Rd; break;
	case fSg | fL: *Rd = (long) *(char*) vaddr(target, 1, &seg); break;
	case fH: case fSg | fH: *(u16int*) vaddr(target, 2, &seg) = *Rd; break;
	case fH | fL: *Rd = *(u16int*) vaddr(target, 2, &seg); break;
	case fH | fL | fSg: *Rd = (long) *(short*) vaddr(target, 2, &seg); break;
	}
	segunlock(seg);
	if(!(instr & fP))
		target += offset;
	if(!(instr & fP) || (instr & fW))
		*Rn = target;
}

static void
block(u32int instr)
{
	int i;
	u32int targ, *Rn;
	Segment *seg;

	if(instr & (1<<22))
		invalid(instr);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rn == P->R + 15 || instr & (1<<15))
		sysfatal("R15 block");
	targ = evenaddr(*Rn, 3);
	if(instr & fU) {
		for(i = 0; i < 16; i++) {
			if(!(instr & (1<<i)))
				continue;
			if(instr & fP)
				targ += 4;
			if(instr & fL)
				P->R[i] = *(u32int*) vaddr(targ, 4, &seg);
			else
				*(u32int*) vaddr(targ, 4, &seg) = P->R[i];
			segunlock(seg);
			if(!(instr & fP))
				targ += 4;
		}
	} else {
		for(i = 15; i >= 0; i--) {
			if(!(instr & (1<<i)))
				continue;
			if(instr & fP)
				targ -= 4;
			if(instr & fL)
				P->R[i] = *(u32int*) vaddr(targ, 4, &seg);
			else
				*(u32int*) vaddr(targ, 4, &seg) = P->R[i];
			segunlock(seg);
			if(!(instr & fP))
				targ -= 4;
		}
	}
	if(instr & fW)
		*Rn = targ;
}

static void
multiply(u32int instr)
{
	u32int *Rd, *Rn, *Rs, *Rm, res;
	
	Rm = P->R + (instr & 15);
	Rs = P->R + ((instr >> 8) & 15);
	Rn = P->R + ((instr >> 12) & 15);
	Rd = P->R + ((instr >> 16) & 15);
	if(Rd == Rm || Rm == P->R + 15 || Rs == P->R + 15 || Rn == P->R + 15 || Rd == P->R + 15)
		invalid(instr);
	res = *Rm * *Rs;
	if(instr & (1<<21))
		res += *Rn;
	*Rd = res;
	if(instr & (1<<20)) {
		P->CPSR &= ~(flN | flZ);
		if(res & (1<<31))
			P->CPSR |= flN;
		if(res == 0)
			P->CPSR |= flZ;
	}
}

static void
multiplylong(u32int instr)
{
	u32int *RdH, *RdL, *Rs, *Rm;
	u64int res;
	
	Rm = P->R + (instr & 15);
	Rs = P->R + ((instr >> 8) & 15);
	RdL = P->R + ((instr >> 12) & 15);
	RdH = P->R + ((instr >> 16) & 15);
	if(RdL == RdH || RdH == Rm || RdL == Rm || Rm == P->R + 15 || Rs == P->R + 15 || RdL == P->R + 15 || RdH == P->R + 15)
		invalid(instr);
	if(instr & (1<<22)) {
		res = *Rs;
		res *= *Rm;
	} else
		res = ((vlong)*(int*)Rs) * *(int*)Rm;
	if(instr & (1<<21)) {
		res += *RdL;
		res += ((uvlong)*RdH) << 32;
	}
	*RdL = res;
	*RdH = res >> 32;
	if(instr & (1<<20)) {
		P->CPSR &= ~FLAGS;
		if(res == 0)
			P->CPSR |= flN;
		if(res & (1LL<<63))
			P->CPSR |= flV;
	}
}

static void
singleex(u32int instr)
{
	u32int *Rn, *Rd, *Rm, *targ, addr;
	Segment *seg;
	
	Rd = P->R + ((instr >> 12) & 15);
	Rn = P->R + ((instr >> 16) & 15);
	if(Rd == P->R + 15 || Rn == P->R + 15)
		invalid(instr);
	addr = evenaddr(*Rn, 3);
	if(instr & fS) {
		targ = vaddr(addr, 4, &seg);
		lock(&seg->lock);
		*Rd = *targ;
		segunlock(seg);
	} else {
		Rm = P->R + (instr & 15);
		if(Rm == P->R + 15)
			invalid(instr);
		targ = vaddr(addr, 4, &seg);
		if(canlock(&seg->lock)) {
			*Rd = 1;
		} else {
			*targ = *Rm;
			unlock(&seg->lock);
			*Rd = 0;
		}
		segunlock(seg);
	}
}

void
step(void)
{
	u32int instr;
	Segment *seg;

	instr = *(u32int*) vaddr(P->PC, 4, &seg);
	segunlock(seg);
	if(fulltrace) {
		print("%d ", P->pid);
		if(havesymbols) {
			Symbol s;
			char buf[512];
			
			if(findsym(P->PC, CTEXT, &s) >= 0)
				print("%s ", s.name);
			if(fileline(buf, 512, P->PC) >= 0)
				print("%s ", buf);
		}
		print("%.16ux %.8ux %c%c%c%c\n", P->PC, instr,
			(P->Z) ? 'Z' : ' ',
			(P->C) ? 'C' : ' ',
			(P->N) ? 'N' : ' ',
			(P->V) ? 'V' : ' '
			);
	}
	P->PC += 4;
	if((instr & 0xFFC00000) == 0xD5000000)
		syscall();
	else if((instr & 0x1C000000) == 0x14000000)
		branch(instr);
	else if((instr & 0x1C000000) == 0x10000000)
		alui(instr);
	else if((instr & 0x0E000000) == 0x0A000000)
		alur(instr);
	else
		invalid(instr);

/*
	else if((instr & 0x0FB00FF0) == 0x01000090)
		swap(instr);
	else if((instr & 0x0FE000F0) == 0x01800090)
		singleex(instr);
	else if((instr & 0x0FC000F0) == 0x90)
		multiply(instr);
	else if((instr & 0x0F8000F0) == 0x800090)
		multiplylong(instr);
	else if((instr & ((1<<26) | (1<<27))) == (1 << 26))
		single(instr);
	else if((instr & 0x0E000090) == 0x90 && (instr & 0x60))
		halfword(instr);
	else if((instr & ((1<<26) | (1<<27))) == 0)
		alu(instr);
	else if((instr & (7<<25)) == (5 << 25))
		branch(instr);
	else if((instr & (15<<24)) == (15 << 24))
		syscall();
	else if((instr & (7<<25)) == (4 << 25))
		block(instr);
	else if((instr & 0x0E000F00) == 0x0C000100)
		fpatransfer(instr);
	else if((instr & 0x0E000F10) == 0x0E000100)
		fpaoperation(instr);
	else if((instr & 0x0E000F10) == 0x0E000110)
		fparegtransfer(instr);
	else if(vfp && ((instr & 0x0F000A10) == 0x0E000A00))
		vfpoperation(instr);
	else if(vfp && ((instr & 0x0F000F10) == 0x0E000A10))
		vfpregtransfer(instr);
	else if(vfp && ((instr & 0x0F000A00) == 0x0D000A00))
		vfprmtransfer(instr);
	else
		invalid(instr);
/*
}
