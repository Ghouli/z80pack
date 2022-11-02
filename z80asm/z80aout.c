/*
 *	Z80 - Macro - Assembler
 *	Copyright (C) 1987-2022 by Udo Munk
 *	Copyright (C) 2022 by Thomas Eberhardt
 *
 *	History:
 *	17-SEP-1987 Development under Digital Research CP/M 2.2
 *	28-JUN-1988 Switched to Unix System V.3
 *	21-OCT-2006 changed to ANSI C for modern POSIX OS's
 *	03-FEB-2007 more ANSI C conformance and reduced compiler warnings
 *	18-MAR-2007 use default output file extension dependent on format
 *	04-OCT-2008 fixed comment bug, ';' string argument now working
 *	22-FEB-2014 fixed is...() compiler warnings
 *	13-JAN-2016 fixed buffer overflow, new expression parser from Didier
 *	02-OCT-2017 bug fixes in expression parser from Didier
 *	28-OCT-2017 added variable symbol length and other improvements
 *	15-MAY-2018 mark unreferenced symbols in listing
 *	30-JUL-2021 fix verbose option
 *	28-JAN-2022 added syntax check for OUT (n),A
 *	24-SEP-2022 added undocumented Z80 instructions and 8080 mode (TE)
 *	04-OCT-2022 new expression parser (TE)
 *	25-OCT-2022 Intel-like macros (TE)
 */

/*
 *	module for output functions to list, object and error files
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "z80a.h"
#include "z80aglb.h"

void lst_byte(BYTE);
void fill_bin(void);
void eof_hex(WORD);
void flush_hex(void);
void hex_record(BYTE);
BYTE chksum(BYTE);
void btoh(BYTE, char **);

/* z80amain.c */
extern void fatal(int, const char *);

/* z80amfun.c */
extern char *mac_lst_first(int, int *);
extern char *mac_lst_next(int, int *);

static const char *errmsg[] = {		/* error messages for asmerr() */
	"no error",			/* 0 */
	"invalid opcode",		/* 1 */
	"invalid operand",		/* 2 */
	"missing operand",		/* 3 */
	"multiple defined symbol",	/* 4 */
	"undefined symbol",		/* 5 */
	"value out of range",		/* 6 */
	"missing right parenthesis",	/* 7 */
	"missing string delimiter",	/* 8 */
	"non-sequential object code",	/* 9 */
	"missing IF",			/* 10 */
	"IF nested too deep",		/* 11 */
	"missing ENDIF",		/* 12 */
	"INCLUDE nested too deep",	/* 13 */
	".PHASE can not be nested",	/* 14 */
	"ORG in .PHASE block",		/* 15 */
	"missing .PHASE",		/* 16 */
	"division by zero",		/* 17 */
	"invalid expression",		/* 18 */
	"object code before ORG",	/* 19 */
	"illegal label",		/* 20 */
	"missing .DEPHASE",		/* 21 */
	"not in macro definition",	/* 22 */
	"missing ENDM",			/* 23 */
	"not in macro expansion",	/* 24 */
	"macro expansion nested too deep", /* 25 */
	"too many local labels"		/* 26 */
};

/*
 *	Intel hex record types
 */
#define	HEX_DATA	0
#define HEX_EOF		1

static int nseq_flag;			/* flag for non-sequential ORG */
static WORD curr_addr;			/* current logical file address */
static WORD bin_addr;			/* current address written to file */
static WORD hex_addr;			/* current address in hex record */
static WORD hex_cnt;			/* number of bytes in hex buffer */

static BYTE hex_buf[MAXHEX];		/* buffer for one hex record */
static char hex_out[MAXHEX*2+13];	/* ASCII buffer for one hex record */

/*
 *	print error message to listfile and increase error counter
 */
void asmerr(int i)
{
	if (pass == 1) {
		fprintf(errfp, "Error in file: %s  Line: %ld\n",
			srcfn, c_line);
		fputs(errmsg[i], errfp);
		fputc('\n', errfp);
	} else
		errnum = i;
	errors++;
}

/*
 *	begin new page in listfile
 */
void lst_header(void)
{
	static int header_done;
	time_t tloc = time(&tloc);

	if (header_done && ppl != 0)
		fputc('\f', lstfp);
	if (!header_done || ppl != 0)
		fprintf(lstfp, "Z80-Macro-Assembler  Release %s\t%.24s",
			REL, ctime(&tloc));
	if (ppl != 0) {
		fprintf(lstfp, "\tPage %d\n", ++page);
		fprintf(lstfp, "Source file: %s\n", srcfn);
		fprintf(lstfp, "Title:       %s", title);
		p_line = 3;
	} else
		p_line = 0;
	fputc('\n', lstfp);
	header_done = 1;
}

/*
 *	print header for source lines
 */
void lst_attl(void)
{
	static int attl_done;

	if (!attl_done || ppl != 0)
		fprintf(lstfp,
			"\nLOC   OBJECT CODE   LINE   STMT SOURCE CODE\n");
	if (ppl != 0)
		p_line += 2;
	attl_done = 1;
}

/*
 *	print one line into listfile, if -l option set
 */
void lst_line(char *l, WORD addr, int op_cnt, int expn_flag)
{
	register int i, j;
	register const char *a_mark;
	static unsigned long s_line;

	s_line++;
	if (!list_flag)
		return;
	if (ppl != 0)
		p_line++;
	if (p_line > ppl || c_line == 1) {
		lst_header();
		lst_attl();
		if (ppl != 0)
			p_line++;
	}
	a_mark = "  ";
	switch (a_mode) {
	case A_STD:
		a_addr = addr;
		break;
	case A_EQU:
		a_mark = "= ";
		break;
	case A_SET:
		a_mark = "# ";
		break;
	case A_DS:
		op_cnt = 0;
		break;
	case A_NONE:
		break;
	default:
		fatal(F_INTERN, "invalid a_mode for function lst_line");
		break;
	}
	if (a_mode == A_NONE)
		fputs("    ", lstfp);
	else {
		lst_byte(a_addr >> 8);
		lst_byte(a_addr & 0xff);
	}
	fputs("  ", lstfp);
	i = 0;
	for (j = 0; j < 4; j++) {
		if (op_cnt-- > 0)
			lst_byte(ops[i++]);
		else if (j == 0)
			fputs(a_mark, lstfp);
		else
			fputs("  ", lstfp);
		fputc(' ', lstfp);
	}
	fprintf(lstfp, "%c%5ld %6ld %s", expn_flag ? '+' : ' ',
		c_line, s_line, l);
	if (errnum != E_NOERR) {
		fprintf(errfp, "=> %s\n", errmsg[errnum]);
		errnum = E_NOERR;
		if (ppl != 0)
			p_line++;
	}
	while (op_cnt > 0) {
		if (ppl != 0)
			p_line++;
		if (p_line > ppl) {
			lst_header();
			lst_attl();
			if (ppl != 0)
				p_line++;
		}
		s_line++;
		addr += 4;
		lst_byte(addr >> 8);
		lst_byte(addr & 0xff);
		fputs("  ", lstfp);
		for (j = 0; j < 4; j++) {
			if (op_cnt-- > 0)
				lst_byte(ops[i++]);
			else
				fputs("  ", lstfp);
			fputc(' ', lstfp);
		}
		fprintf(lstfp, "%c%5ld %6ld\n", expn_flag ? '+' : ' ',
			c_line, s_line);
	}
	if (p_line < 0)
		p_line = 1;
}

/*
 *	print macro table into listfile
 */
void lst_mac(int sorted)
{
	register int i, j;
	register char *p;
	int refcnt;

	p_line = i = 0;
	strcpy(title, "Macro table");
	for (p = mac_lst_first(sorted, &refcnt); p != NULL;
	     p = mac_lst_next(sorted, &refcnt)) {
		if (p_line == 0) {
			lst_header();
			if (ppl == 0) {
				fputs(title, lstfp);
				fputc('\n', lstfp);
			}
			fputc('\n', lstfp);
			p_line++;
		}
		j = strlen(p);
		fprintf(lstfp, "%s%c", p, refcnt > 0 ? ' ' : '*');
		while (j++ < mac_symmax)
			fputc(' ', lstfp);
		i += mac_symmax + 4;
		if (i + mac_symmax + 1 >= 80) {
			fputc('\n', lstfp);
			if (ppl != 0 && ++p_line >= ppl)
				p_line = 0;
			i = 0;
		} else
			fputs("   ", lstfp);
	}
	if (i > 0)
		fputc('\n', lstfp);
}

/*
 *	print symbol table into listfile unsorted
 */
void lst_sym(void)
{
	register int i, j;
	register struct sym *np;

	p_line = j = 0;
	strcpy(title, "Symbol table");
	for (i = 0; i < HASHSIZE; i++) {
		if (symtab[i] != NULL) {
			for (np = symtab[i]; np != NULL; np = np->sym_next) {
				if (p_line == 0) {
					lst_header();
					if (ppl == 0) {
						fputs(title, lstfp);
						fputc('\n', lstfp);
					}
					fputc('\n', lstfp);
					p_line++;
				}
				fprintf(lstfp, "%*s ", -symmax, np->sym_name);
				lst_byte(np->sym_val >> 8);
				lst_byte(np->sym_val & 0xff);
				fputc(np->sym_refcnt > 0 ? ' ' : '*', lstfp);
				j += symmax + 9;
				if (j + symmax + 6 >= 80) {
					fputc('\n', lstfp);
					if (ppl != 0 && ++p_line >= ppl)
						p_line = 0;
					j = 0;
				} else
					fputs("   ", lstfp);
			}
		}
	}
	if (j > 0)
		fputc('\n', lstfp);
}

/*
 *	print sorted symbol table into listfile
 */
void lst_sort_sym(int len)
{
	register int i, j;

	p_line = i = j = 0;
	strcpy(title, "Symbol table");
	while (i < len) {
		if (p_line == 0) {
			lst_header();
			if (ppl == 0) {
				fputs(title, lstfp);
				fputc('\n', lstfp);
			}
			fputc('\n', lstfp);
			p_line++;
		}
		fprintf(lstfp, "%*s ", -symmax, symarray[i]->sym_name);
		lst_byte(symarray[i]->sym_val >> 8);
		lst_byte(symarray[i]->sym_val & 0xff);
		fputc(symarray[i]->sym_refcnt > 0 ? ' ' : '*', lstfp);
		j += symmax + 9;
		if (j + symmax + 6 >= 80) {
			fputc('\n', lstfp);
			if (ppl != 0 && ++p_line >= ppl)
				p_line = 0;
			j = 0;
		} else
			fputs("   ", lstfp);
		i++;
	}
	if (j > 0)
		fputc('\n', lstfp);
}

/*
 *	print BYTE as ASCII hex into listfile
 */
void lst_byte(BYTE b)
{
	register char c;

	c = b >> 4;
	fputc(c + (c < 10 ? '0' : 'W'), lstfp);
	c = b & 0xf;
	fputc(c + (c < 10 ? '0' : 'W'), lstfp);
}

/*
 *	write header record into object file
 */
void obj_header(void)
{
	switch (obj_fmt) {
	case OBJ_BIN:
		break;
	case OBJ_MOS:
		putc(0xff, objfp);
		putc(load_addr & 0xff, objfp);
		putc(load_addr >> 8, objfp);
		break;
	case OBJ_HEX:
		break;
	}
}

/*
 *	write end record into object file
 */
void obj_end(void)
{
	switch (obj_fmt) {
	case OBJ_BIN:
	case OBJ_MOS:
		if (!nofill_flag && !(load_flag && (bin_addr < load_addr)))
			fill_bin();
		break;
	case OBJ_HEX:
		flush_hex();
		eof_hex(start_addr);
		break;
	}
}

/*
 *	set logical address for object file
 */
void obj_org(WORD addr)
{
	switch (obj_fmt) {
	case OBJ_BIN:
	case OBJ_MOS:
		nseq_flag = (addr < curr_addr);
		if (load_flag && (bin_addr < load_addr))
			bin_addr = addr;
		curr_addr = addr;
		break;
	case OBJ_HEX:
		curr_addr = addr;
		break;
	}
}

/*
 *	write opcodes in ops[] into object file
 */
void obj_writeb(int op_cnt)
{
	register int i;

	if (op_cnt == 0)
		return;
	switch (obj_fmt) {
	case OBJ_BIN:
	case OBJ_MOS:
		if (nseq_flag)
			asmerr(E_NSQWRT);
		else {
			if (load_flag && (bin_addr < load_addr))
				asmerr(E_BFRORG);
			else {
				fill_bin();
				fwrite(ops, 1, op_cnt, objfp);
				bin_addr += op_cnt;
			}
			curr_addr += op_cnt;
		}
		break;
	case OBJ_HEX:
		if (hex_addr + hex_cnt != curr_addr)
			flush_hex();
		for (i = 0; op_cnt > 0; op_cnt--) {
			if (hex_cnt >= hexlen)
				flush_hex();
			hex_buf[hex_cnt++] = ops[i++];
			curr_addr++;
		}
		break;
	}
}

/*
 *	advance logical address of object file by count
 */
void obj_fill(WORD count)
{
	if (count == 0)
		return;
	switch (obj_fmt) {
	case OBJ_BIN:
	case OBJ_MOS:
		if (!nseq_flag)
			curr_addr += count;
		break;
	case OBJ_HEX:
		curr_addr += count;
		break;
	}
}

/*
 *	write <count> bytes <value> into object file
 */
void obj_fill_value(WORD count, WORD value)
{
	register WORD n;

	if (count == 0)
		return;
	n = count;
	switch (obj_fmt) {
	case OBJ_BIN:
	case OBJ_MOS:
		if (nseq_flag)
			asmerr(E_NSQWRT);
		else {
			if (load_flag && (bin_addr < load_addr))
				asmerr(E_BFRORG);
			else {
				fill_bin();
				while (n-- > 0)
					putc(value, objfp);
				bin_addr += count;
			}
			curr_addr += count;
		}
		break;
	case OBJ_HEX:
		if (hex_addr + hex_cnt != curr_addr)
			flush_hex();
		while (n-- > 0) {
			if (hex_cnt >= hexlen)
				flush_hex();
			hex_buf[hex_cnt++] = value;
			curr_addr++;
		}
		break;
	}
}

/*
 *	fill binary object file up to the logical address with 0xff bytes
 */
void fill_bin(void)
{
	while (bin_addr < curr_addr) {
		putc(0xff, objfp);
		bin_addr++;
	}
}

/*
 *	create a hex end-of-file record in ASCII and write into object file
 */
void eof_hex(WORD addr)
{
	hex_cnt = 0;
	hex_addr = addr;
	hex_record(HEX_EOF);
}

/*
 *	create a hex data record in ASCII and write into object file
 */
void flush_hex(void)
{
	if (hex_cnt != 0) {
		hex_record(HEX_DATA);
		hex_cnt = 0;
	}
	hex_addr = curr_addr;
}

/*
 *	write a hex record in ASCII and write into object file
 */
void hex_record(BYTE rec_type)
{
	register WORD n;
	char *p;

	p = hex_out;
	*p++ = ':';
	btoh(hex_cnt, &p);
	btoh(hex_addr >> 8, &p);
	btoh(hex_addr & 0xff, &p);
	btoh(rec_type, &p);
	for (n = 0; n < hex_cnt; n++)
		btoh(hex_buf[n], &p);
	btoh(chksum(rec_type), &p);
	*p++ = '\n';
	*p = '\0';
	fputs(hex_out, objfp);
}

/*
 *	convert BYTE into ASCII hex and copy to string at p
 *	increase p by 2
 */
void btoh(BYTE b, char **p)
{
	register char c;

	c = b >> 4;
	*(*p)++ = c + (c < 10 ? '0' : '7');
	c = b & 0xf;
	*(*p)++ = c + (c < 10 ? '0' : '7');
}

/*
 *	compute checksum for Intel hex record
 */
BYTE chksum(BYTE rec_type)
{
	register WORD n;
	register BYTE sum;

	sum = hex_cnt;
	sum += hex_addr >> 8;
	sum += hex_addr & 0xff;
	sum += rec_type;
	for (n = 0; n < hex_cnt; n++)
		sum += hex_buf[n];
	return(-sum);
}
