
/* recBi.c */
/* share/src/rec $Id$ */

/* recBi.c - Record Support Routines for Binary Input records
 *
 * Author: 	Bob Dalesio
 * Date:	7-9-87
 *
 *	Control System Software for the GTA Project
 *
 *	Copyright 1988, 1989, the Regents of the University of California.
 *
 *	This software was produced under a U.S. Government contract
 *	(W-7405-ENG-36) at the Los Alamos National Laboratory, which is
 *	operated by the University of California for the U.S. Department
 *	of Energy.
 *
 *	Developed by the Controls and Automation Group (AT-8)
 *	Accelerator Technology Division
 *	Los Alamos National Laboratory
 *
 *	Direct inqueries to:
 *	Bob Dalesio, AT-8, Mail Stop H820
 *	Los Alamos National Laboratory
 *	Los Alamos, New Mexico 87545
 *	Phone: (505) 667-3414
 *	E-mail: dalesio@luke.lanl.gov
 *
 * Modification Log:
 * -----------------
 * .01  12-12-88        lrd     lock the record on entry and unlock on exit
 * .02  12-15-88        lrd     Process the forward scan link
 * .03  12-23-88        lrd     Alarm on locked MAX_LOCKED times
 * .04  01-13-89        lrd     delete db_read_bi
 * .05  03-17-89        lrd     database link inputs
 * .06  03-29-89        lrd     make hardware errors MAJOR
 *                              remove hw severity spec from database
 * .07  04-06-89        lrd     add monitor detection
 * .08  05-03-89        lrd     removed process mask from arg list
 * .09  01-31-90        lrd     add the plc_flag arg to the ab_bidriver call
 * .10  03-21-90        lrd     add db_post_events for RVAL
 * .11  04-11-90        lrd     make local variables static
 * .12  10-10-90	mrk	Made changes for new record and device support
 */

#include	<vxWorks.h>
#include	<types.h>
#include	<stdioLib.h>
#include	<lstLib.h>
#include	<strLib.h>

#include	<alarm.h>
#include	<cvtTable.h>
#include	<dbAccess.h>
#include	<dbDefs.h>
#include	<dbFldTypes.h>
#include	<devSup.h>
#include	<errMdef.h>
#include	<link.h>
#include	<recSup.h>
#include	<special.h>
#include	<biRecord.h>

/* Create RSET - Record Support Entry Table*/
long report();
#define initialize NULL
long init_record();
long process();
long special();
long get_precision();
long get_value();
#define cvt_dbaddr NULL
#define get_array_info NULL
#define put_array_info NULL
long get_enum_str();
#define get_units NULL
#define get_graphic_double NULL
#define get_control_double NULL
long get_enum_strs();

struct rset biRSET={
	RSETNUMBER,
	report,
	initialize,
	init_record,
	process,
	special,
	get_precision,
	get_value,
	cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_enum_str,
	get_units,
	get_graphic_double,
	get_control_double,
	get_enum_strs };

struct bidset { /* binary input dset */
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record; /*returns: (-1,0)=>(failure,success)*/
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_bi;/*(-1,0,1)=>(failure,success,don't Continue*/
};

static long report(fp,paddr)
    FILE	  *fp;
    struct dbAddr *paddr;
{
    struct biRecord	*pbi=(struct biRecord*)(paddr->precord);

    if(recGblReportDbCommon(fp,paddr)) return(-1);
    if(fprintf(fp,"VAL  %d\n",pbi->val)) return(-1);
    if(recGblReportLink(fp,"INP ",&(pbi->inp))) return(-1);
    if(recGblReportLink(fp,"FLNK",&(pbi->flnk))) return(-1);
    if(fprintf(fp,"RVAL 0x%-8X\n",
	pbi->rval)) return(-1);
    return(0);
}

static long init_record(pbi)
    struct biRecord	*pbi;
{
    struct bidset *pdset;
    long status;

    if(!(pdset = (struct bidset *)(pbi->dset))) {
	recGblRecordError(S_dev_noDSET,pbi,"bi: init_record");
	return(S_dev_noDSET);
    }
    /* must have read_bi function defined */
    if( (pdset->number < 5) || (pdset->read_bi == NULL) ) {
	recGblRecordError(S_dev_missingSup,pbi,"bi: init_record");
	return(S_dev_missingSup);
    }
    if( pdset->init_record ) {
	if((status=(*pdset->init_record)(pbi))) return(status);
    }
    return(0);
}

static long process(paddr)
    struct dbAddr	*paddr;
{
    struct biRecord	*pbi=(struct biRecord *)(paddr->precord);
	struct bidset	*pdset = (struct bidset *)(pbi->dset);
	long		 status;

	if( (pdset==NULL) || (pdset->read_bi==NULL) ) {
		pbi->pact=TRUE;
		recGblRecordError(S_dev_missingSup,pbi,"read_bi");
		return(S_dev_missingSup);
	}

	status=(*pdset->read_bi)(pbi); /* read the new value */
	pbi->pact = TRUE;

	/* status is one if an asynchronous record is being processed*/
	if(status==1) return(1);
	else if(status == -1) {
		if(pbi->stat != READ_ALARM) {/* error. set alarm condition */
			pbi->stat = READ_ALARM;
			pbi->sevr = MAJOR_ALARM;
			pbi->achn=1;
		}
	}
	else if(status!=0) return(status);
	else if(pbi->stat == READ_ALARM) {
		pbi->stat = NO_ALARM;
		pbi->sevr = NO_ALARM;
		pbi->achn=1;
	}

	/* check for alarms */
	alarm(pbi);


	/* check event list */
	if(!pbi->disa) status = monitor(pbi);

	/* process the forward scan link record */
	if (pbi->flnk.type==DB_LINK) dbScanPassive(&pbi->flnk.value);

	pbi->pact=FALSE;
	return(status);
}

static long get_value(pbi,pvdes)
    struct biRecord		*pbi;
    struct valueDes	*pvdes;
{
    pvdes->field_type = DBF_ENUM;
    pvdes->no_elements=1;
    (unsigned short *)(pvdes->pvalue) = &pbi->val;
    return(0);
}

static long get_enum_str(paddr,pstring)
    struct dbAddr *paddr;
    char	  *pstring;
{
    struct biRecord	*pbi=(struct biRecord *)paddr->precord;

    if(pbi->val==0) {
	strncpy(pstring,pbi->znam,sizeof(pbi->znam));
	pstring[sizeof(pbi->znam)] = 0;
    } else if(pbi->val==0) {
	strncpy(pstring,pbi->onam,sizeof(pbi->onam));
	pstring[sizeof(pbi->onam)] = 0;
    } else {
	strcpy(pstring,"Illegal Value");
    }
    return(0L);
}

static long get_enum_strs(paddr,pes)
    struct dbAddr *paddr;
    struct dbr_enumStrs *pes;
{
    struct biRecord	*pbi=(struct biRecord *)paddr->precord;

    pes->no_str = 2;
    bzero(pes->strs,sizeof(pes->strs));
    strncpy(pes->strs[0],pbi->znam,sizeof(pbi->znam));
    strncpy(pes->strs[1],pbi->onam,sizeof(pbi->onam));
    return(0L);
}

static long alarm(pbi)
    struct biRecord	*pbi;
{
	float	ftemp;

	/* check for a hardware alarm */
	if (pbi->stat == READ_ALARM) return(0);

        if (pbi->val == pbi->lalm){
                /* no new message for COS alarms */
                if (pbi->stat == COS_ALARM){
                        pbi->stat = NO_ALARM;
                        pbi->sevr = NO_ALARM;
                }
                return;
        }

        /* set last alarmed value */
        pbi->lalm = pbi->val;

        /* check for  state alarm */
        if (pbi->val == 0){
                if (pbi->zsv != NO_ALARM){
                        pbi->stat = STATE_ALARM;
                        pbi->sevr = pbi->zsv;
                        pbi->achn = 1;
                        return;
                }
        }else{
                if (pbi->osv != NO_ALARM){
                        pbi->stat = STATE_ALARM;
                        pbi->sevr = pbi->osv;
                        pbi->achn = 1;
                        return;
                }
        }

        /* check for cos alarm */
        if (pbi->cosv != NO_ALARM){
                pbi->sevr = pbi->cosv;
                pbi->stat = COS_ALARM;
                pbi->achn = 1;
                return;
        }

        /* check for change from alarm to no alarm */
        if (pbi->sevr != NO_ALARM){
                pbi->sevr = NO_ALARM;
                pbi->stat = NO_ALARM;
                pbi->achn = 1;
                return;
        }

	return(0);
}

static long monitor(pbi)
    struct biRecord	*pbi;
{
	unsigned short	monitor_mask;

	/* anyone waiting for an event on this record */
	if (pbi->mlis.count == 0) return(0L);

	/* Flags which events to fire on the value field */
	monitor_mask = 0;

	/* alarm condition changed this scan */
	if (pbi->achn){
		/* post events for alarm condition change and value change */
		monitor_mask = DBE_ALARM | DBE_VALUE | DBE_LOG;

		/* post stat and sevr fields */
		db_post_events(pbi,&pbi->stat,DBE_VALUE);
		db_post_events(pbi,&pbi->sevr,DBE_VALUE);

		/* update last value monitored */
		pbi->mlst = pbi->val;
        /* check for value change */
        }else if (pbi->mlst != pbi->val){
                /* post events for value change and archive change */
                monitor_mask |= (DBE_VALUE | DBE_LOG);

                /* update last value monitored */
                pbi->mlst = pbi->val;
        }

	/* send out monitors connected to the value field */
	if (monitor_mask){
		db_post_events(pbi,&pbi->val,monitor_mask);
		db_post_events(pbi,&pbi->rval,monitor_mask);
	}
	return(0L);
}
