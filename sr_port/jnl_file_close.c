/****************************************************************
 *								*
 *	Copyright 2003, 2010 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_time.h"
#include "gtm_string.h"

#if defined(UNIX)
#include "gtm_unistd.h"
#include "aswp.h"
#include "lockconst.h"
#include "interlock.h"
#include "sleep_cnt.h"
#include "performcaslatchcheck.h"
#include "wcs_sleep.h"
#include "gt_timer.h"
#elif defined(VMS)
#include <rms.h>
#include <iodef.h>
#include <psldef.h>
#include <ssdef.h>
#include <efndef.h>
#include "iosb_disk.h"
#endif

#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsblk.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "jnl.h"
#include "jnl_write.h"
#include "gtmio.h"
#include "repl_sp.h"	/* F_CLOSE */
#include "iosp.h"	/* for SS_NORMAL */
#include "ccp.h"
#include "send_msg.h"
#include "eintr_wrappers.h"

#ifdef UNIX
#include "wcs_clean_dbsync.h"
#endif

#if defined(VMS)
GBLREF	short	astq_dyn_avail;
static	const	unsigned short	zero_fid[3];
#endif

void	jnl_file_close(gd_region *reg, bool clean, bool dummy)
{
	jnl_file_header		*header;
	unsigned char		hdr_base[REAL_JNL_HDR_LEN + MAX_IO_BLOCK_SIZE];
	sgmnt_addrs		*csa;
	sgmnt_data_ptr_t	csd;
	jnl_private_control	*jpc;
	jnl_buffer_ptr_t	jb;
	struct_jrec_eof		eof_record;
	off_jnl_t		eof_addr;
	uint4			status, read_write_size;
	int			rc, save_errno;
	uint4			jnl_fs_block_size;

	error_def(ERR_JNLCLOSE);
	error_def(ERR_JNLFLUSH);
	error_def(ERR_JNLFSYNCERR);
	error_def(ERR_JNLWRERR);
	error_def(ERR_PREMATEOF);
	error_def(ERR_TEXT);

	csa = &FILE_INFO(reg)->s_addrs;
	csd = csa->hdr;
	assert(!clean || csa->now_crit || (csd->clustered && (CCST_CLOSED == csa->nl->ccp_state)));
	DEBUG_ONLY(
		if (clean)
			ASSERT_JNLFILEID_NOT_NULL(csa);
	)
	jpc = csa->jnl;
#if defined(UNIX)
	if (csa->dbsync_timer)
		CANCEL_DBSYNC_TIMER(csa, FALSE);
#elif defined(VMS)
	/* See comment about ordering of the two statements below, in similar code in gds_rundown */
	if (csa->dbsync_timer)
	{
		csa->dbsync_timer = FALSE;
		++astq_dyn_avail;
	}
	sys$cantim(csa, PSL$C_USER);	/* cancel all dbsync-timers for this region */
#endif
	if ((NULL == jpc) || (NOJNL == jpc->channel))
		return;
	jb = jpc->jnl_buff;
	jnl_fs_block_size = jb->fs_block_size;
	header = (jnl_file_header *)(ROUND_UP2((uintszofptr_t)hdr_base, jnl_fs_block_size));
	if (clean)
	{
		jnl_write_eof_rec(csa, &eof_record);
		if (SS_NORMAL != (jpc->status = jnl_flush(reg)))
		{
			send_msg(VARLSTCNT(9) ERR_JNLFLUSH, 2, JNL_LEN_STR(csd),
				ERR_TEXT, 2, RTS_ERROR_TEXT("Error with journal flush during jnl_file_close"),
				jpc->status);
			assert(FALSE);
			rts_error(VARLSTCNT(9) ERR_JNLFLUSH, 2, JNL_LEN_STR(csd),
				ERR_TEXT, 2, RTS_ERROR_TEXT("Error with journal flush during jnl_file_close"),
				jpc->status);
		}
		assert(jb->dskaddr == jb->freeaddr);
		UNIX_ONLY(jnl_fsync(reg, jb->dskaddr);)
		UNIX_ONLY(assert(jb->freeaddr == jb->fsync_dskaddr);)
		eof_addr = jb->freeaddr - EOF_RECLEN;
		read_write_size = ROUND_UP2(REAL_JNL_HDR_LEN, jnl_fs_block_size);
		assert((unsigned char *)header + read_write_size <= ARRAYTOP(hdr_base));
		DO_FILE_READ(jpc->channel, 0, header, read_write_size, jpc->status, jpc->status2);
		if (SYSCALL_SUCCESS(jpc->status))
		{
			assert(header->end_of_data <= eof_addr);
			header->end_of_data = eof_addr;
			header->eov_timestamp = eof_record.prefix.time;
			assert(header->eov_timestamp >= header->bov_timestamp);
			header->eov_tn = eof_record.prefix.tn;
			assert(header->eov_tn >= header->bov_tn);
			header->end_seqno = eof_record.jnl_seqno;
			header->crash = FALSE;
			DO_FILE_WRITE(jpc->channel, 0, header, read_write_size, jpc->status, jpc->status2);
			if (SYSCALL_ERROR(jpc->status))
			{
				assert(FALSE);
				rts_error(VARLSTCNT(5) ERR_JNLWRERR, 2, JNL_LEN_STR(csd), jpc->status);
			}
			UNIX_ONLY(
				GTM_FSYNC(jpc->channel, rc);
				if (-1 == rc)
				{
					save_errno = errno;
					send_msg(VARLSTCNT(9) ERR_JNLFSYNCERR, 2, JNL_LEN_STR(csd),
						ERR_TEXT, 2, RTS_ERROR_TEXT("Error with fsync during jnl_file_close"), save_errno);
					assert(FALSE);
					rts_error(VARLSTCNT(9) ERR_JNLFSYNCERR, 2, JNL_LEN_STR(csd),
						ERR_TEXT, 2, RTS_ERROR_TEXT("Error with fsync during jnl_file_close"), save_errno);
				}
			)
		}
		/* jnl_file_id should be nullified only after the jnl file header has been written to disk.
		 * Nullifying the jnl_file_id signals that the jnl file has been switched. The replication source server
		 * assumes that the jnl file has been completely written to disk (including the header) before the switch is
		 * signalled.
		 */
		NULLIFY_JNL_FILE_ID(csa);
		jb->cycle++;	/* increment shared cycle so all future callers of jnl_ensure_open recognize journal switch */
	}
	JNL_FD_CLOSE(jpc->channel, rc);	/* sets jpc->channel to NOJNL */
	jpc->cycle--;	/* decrement cycle so jnl_ensure_open() knows to reopen the journal */
	VMS_ONLY(jpc->qio_active = FALSE;)
	jpc->pini_addr = 0;
	if (clean && (SS_NORMAL != jpc->status))
	{
		status = jpc->status;	/* jnl_send_oper resets jpc->status, so save it */
		jnl_send_oper(jpc, ERR_JNLCLOSE);
		rts_error(VARLSTCNT(5) ERR_JNLCLOSE, 2, JNL_LEN_STR(csd), status);
	}
}
