/****************************************************************
 *								*
 *	Copyright 2001, 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include "compiler.h"
#include "rtnhdr.h"
#include "mdq.h"
#include "stringpool.h"

GBLREF mliteral		literal_chain;
GBLREF spdesc		stringpool;
GBLREF unsigned short	source_name_len;
GBLREF mident		routine_name;

GBLDEF uint4		lits_text_size, lits_mval_size;

void comp_lits(rhdtyp *rhead)
{
  	size_t		offset;
	uint4		cnt;
	uint4 		align_pad;
	mliteral	*p;

	/* Literal text pool is formed in stringpool except for the file name/path of the
	 * source module and routine name which will be emitted by emit_literals immediately
	 * following the literal text pool and is considered part of that text pool.*/
	offset = (stringpool.free - stringpool.base);
	offset += PADLEN(offset, NATIVE_WSIZE);
	rhead->src_full_name.len = source_name_len;
	rhead->src_full_name.addr = (char *)offset;
	offset += source_name_len;
	offset += PADLEN(offset, NATIVE_WSIZE);
	rhead->routine_name.len = routine_name.len;
	rhead->routine_name.addr = (char *)offset;
	offset += routine_name.len;
	offset += PADLEN(offset, NATIVE_WSIZE);
	lits_text_size = UINTCAST(offset);
	offset = 0;
	dqloop(&literal_chain, que, p)
		if (p->rt_addr < 0)
		{
			p->rt_addr = offset;
			offset += SIZEOF(mval);
		}
	lits_mval_size = UINTCAST(offset);
}
