/* Copyright (C) 2003 Datapark corp. All rigths reserved.
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

#ifndef _DPS_DOC_H
#define _DPS_DOC_H

extern __C_LINK DPS_DOCUMENT * __DPSCALL DpsDocInit(DPS_DOCUMENT*);
extern __C_LINK void __DPSCALL DpsDocFree(DPS_DOCUMENT*);
extern char * DpsDocToTextBuf(DPS_DOCUMENT * Doc);
extern __C_LINK int __DPSCALL DpsDocFromTextBuf(DPS_DOCUMENT * Doc, const char *d);
extern int DpsDocLookupConn(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc);
extern int DpsDocAddDocExtraHeaders(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc);
extern int DpsDocAddConfExtraHeaders(DPS_ENV *Conf, DPS_DOCUMENT *Doc);
extern int DpsDocAddServExtraHeaders(DPS_SERVER *Server, DPS_DOCUMENT *Doc);
extern int DpsDocProcessResponseHeaders(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc);

extern void DpsURLCRDListListFree(DPS_URLCRDLISTLIST *Lst);

#endif
