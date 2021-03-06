/****************************************************************
 *								*
 *	Copyright 2001, 2010 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef HAVE_CRIT_H_INCLUDED
#define HAVE_CRIT_H_INCLUDED

/* states of CRIT passed as argument to have_crit() */
#define CRIT_IN_COMMIT		0x00000001
#define CRIT_NOT_TRANS_REG	0x00000002
#define CRIT_RELEASE		0x00000004
#define CRIT_ALL_REGIONS	0x00000008

#define	CRIT_IN_WTSTART		0x00000010	/* check if csa->in_wtstart is true */

/* Note absence of any flags is default value which finds if any region
   or the replication pool have crit or are getting crit. It returns
   when one is found without checking further.
*/
#define CRIT_HAVE_ANY_REG	0x00000000

typedef enum
{
	INTRPT_OK_TO_INTERRUPT = 0,
	INTRPT_IN_GTCMTR_TERMINATE,
	INTRPT_IN_TP_UNWIND,
	INTRPT_IN_TP_CLEAN_UP,
	INTRPT_IN_CRYPT_SECTION,
	INTRPT_IN_DB_CSH_GETN,
	INTRPT_IN_GVCST_INIT,
	INTRPT_IN_GDS_RUNDOWN,
	INTRPT_IN_SS_INITIATE,
	INTRPT_IN_ZLIB_CMP_UNCMP,
	INTRPT_IN_TRIGGER_NOMANS_LAND,	/* State where have trigger base frame but no trigger (exec) frame */
	INTRPT_IN_MUR_OPEN_FILES,
	INTRPT_NUM_STATES,
} intrpt_state_t;

GBLREF	intrpt_state_t	intrpt_ok_state;

/* Macro to check if we are in a state that is ok to interrupt (or to do deferred signal handling).
 * We do not want to interrupt if the global variable intrpt_ok_state indicates it is not ok to interrupt,
 * if we are in the midst of a malloc, if we are holding crit, if we are in the midst of commit, or in
 * wcs_wtstart. In the last case, we could be causing another process HOLDING CRIT on the region to wait
 * in bg_update_phase1 if we hold the write interlock. Hence it is important for us to finish that as soon
 * as possible and not interrupt it.
 */
#define	OK_TO_INTERRUPT	((INTRPT_OK_TO_INTERRUPT == intrpt_ok_state) && (0 == gtmMallocDepth)			\
				&& (0 == have_crit(CRIT_HAVE_ANY_REG | CRIT_IN_COMMIT | CRIT_IN_WTSTART)))

/* Macro to be used whenever we want to handle any signals that we deferred handling and exit in the process.
 * In VMS, we dont do any signal handling, only exit handling.
 */
#define	DEFERRED_EXIT_HANDLING_CHECK									\
{													\
	VMS_ONLY(GBLREF	int4	exi_condition;)								\
	GBLREF	int		process_exiting;							\
	GBLREF	VSIG_ATOMIC_T	forced_exit;								\
	GBLREF	volatile int4	gtmMallocDepth;								\
													\
	if (forced_exit && !process_exiting && OK_TO_INTERRUPT)						\
		UNIX_ONLY(deferred_signal_handler();)							\
		VMS_ONLY(sys$exit(exi_condition);)							\
}

/* Macro to cause deferrable interrupts to be deferred recording the cause.
 * If interrupt is already deferred, state is not changed.
 *
 * The normal usage of the below macros is
 *	DEFER_INTERRUPTS
 *	non-interruptible code
 *	ENABLE_INTERRUPTS
 * We want the non-interruptible code to be executed AFTER the SAVE_INTRPT_OK_STATE macro.
 * To enforce this ordering, one would think a read memory barrier is needed in between.
 * But it is not needed. This is because we expect the non-interruptible code to have
 *	a) pointer dereferences OR
 *	b) function calls
 * Either of these will prevent the compiler from reordering the non-interruptible code.
 * Any non-interruptible code that does not have either of the above usages (for e.g. uses C global
 * variables) might be affected by compiler reordering. As of now, there is no known case of such
 * usage and no such usage is anticipated in the future.
 *
 * We dont need to worry about machine reordering as well since there is no shared memory variable
 * involved here (intrpt_ok_state is a process private variable) and even if any reordering occurs
 * they will all be in-flight instructions when the interrupt occurs so the hardware will guarantee
 * all such instructions are completely done or completely discarded before servicing the interrupt
 * which means the interrupt service routine will never see a reordered state of the above code.
 */
#define DEFER_INTERRUPTS(NEWSTATE)									\
{													\
	if (INTRPT_OK_TO_INTERRUPT == intrpt_ok_state)							\
		/* Only reset state if we are in "OK" state */						\
		intrpt_ok_state = NEWSTATE;								\
	else												\
		assert((NEWSTATE) != intrpt_ok_state);	/* Make sure not nesting same code */		\
}

/* Re-enable deferrable interrupts if the expected state is found. If expected state is not found, then
 * we must have nested interrupt types. Avoid state changes in that case. When the nested state pops,
 * interrupts will be restored.
 */
#define ENABLE_INTERRUPTS(OLDSTATE)									\
{													\
	assert(((OLDSTATE) == intrpt_ok_state) || (INTRPT_OK_TO_INTERRUPT != intrpt_ok_state));		\
	if ((OLDSTATE) == intrpt_ok_state)								\
	{	/* Only reset state if in expected state - othwise state must be non-zero which is	\
		 * asserted above.									\
		 */											\
		intrpt_ok_state = INTRPT_OK_TO_INTERRUPT;						\
		DEFERRED_EXIT_HANDLING_CHECK;	/* check if signals were deferred while held lock */	\
	}												\
}

uint4 have_crit(uint4 crit_state);

#endif /* HAVE_CRIT_H_INCLUDED */

