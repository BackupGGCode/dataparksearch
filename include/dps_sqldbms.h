/* Copyright (C) 2003-2009 Datapark corp. All rights reserved.
   Copyright (C) 2000-2002 Lavtech.com corp. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

#ifndef DPS_SQLDBMS_H
#define DPS_SQLDBMS_H

char            *DpsDBEscDoubleStr(char *from);
char		*DpsDBEscStr(int DBType,char *to,const char *from,size_t len);
extern __C_LINK int __DPSCALL _DpsSQLQuery(DPS_DB *db, DPS_SQLRES *R, const char *query, const char *file, const int line);
#if defined(HAVE_DP_PGSQL) || defined(HAVE_DP_MYSQL)
extern __C_LINK int __DPSCALL _DpsSQLAsyncQuery(DPS_DB *db, DPS_SQLRES *R, const char *query, const char *file, const int line);
#endif
extern __C_LINK size_t __DPSCALL DpsSQLNumRows(DPS_SQLRES *res);
size_t		DpsSQLNumCols(DPS_SQLRES *res);
size_t		DpsSQLLen(DPS_SQLRES * res,size_t i,size_t j);
extern __C_LINK const char * __DPSCALL DpsSQLValue(DPS_SQLRES * res,size_t i,size_t j);
extern __C_LINK void __DPSCALL DpsSQLFree(DPS_SQLRES*);
void		DpsSQLClose(DPS_DB *db);
extern int __DPSCALL DpsSQLBegin(DPS_DB *db);
extern int __DPSCALL DpsSQLEnd(DPS_DB *db);
extern int __DPSCALL DpsSQLAbort(DPS_DB *db);
extern void DpsSQLResInit(DPS_SQLRES *SQLRes);

#define DpsSQLQuery(db, R, query) _DpsSQLQuery(db, R, query, __FILE__, __LINE__)
#if defined(HAVE_DP_PGSQL) || defined(HAVE_DP_MYSQL)
#define DpsSQLAsyncQuery(db, R, query) _DpsSQLAsyncQuery(db, R, query, __FILE__, __LINE__)
/*#define DpsSQLAsyncQuery(db, R, query) _DpsSQLQuery(db, R, query, __FILE__, __LINE__)*/
#else
#define DpsSQLAsyncQuery(db, R, query) _DpsSQLQuery(db, R, query, __FILE__, __LINE__)
#endif

#define DPS_SQLMON_DISPLAY_FIELDS	1
#define DPS_SQLMON_MSG_ERROR		1
#define DPS_SQLMON_MSG_PROMPT		2

typedef struct dps_sqlmon_param_st
{
  int flags;
  size_t nqueries;
  size_t ngood;
  size_t nbad;
  FILE *infile;
  FILE *outfile;
  char *(*gets)(struct dps_sqlmon_param_st *prm, char *str, size_t size);
  int (*display)(struct dps_sqlmon_param_st *, DPS_SQLRES *sqlres);
  int (*prompt)(struct dps_sqlmon_param_st *, int msgtype, const char *msg);
  void* context_ptr;
} DPS_SQLMON_PARAM;

extern __C_LINK int __DPSCALL DpsSQLMonitor(DPS_AGENT *A, DPS_ENV *E, DPS_SQLMON_PARAM *prm);
extern __C_LINK const char* __DPSCALL DpsIndCmdStr(enum dps_indcmd cmd);

#endif
