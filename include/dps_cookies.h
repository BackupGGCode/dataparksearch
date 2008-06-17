/* Copyright (C) 2006 Datapark corp. All rights reserved.

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

#ifndef _DPS_COOKIES_H
#define _DPS_COOKIES_H

extern int DpsCookiesAdd(DPS_AGENT *Indexer, const char *domain, const char * path, const char *name, const char *value, const char secure,
			 dps_uint4 expires, int insert_flag);
extern void DpsCookiesFind(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc, const char *hostinfo);
extern void DpsCookiesFree(DPS_COOKIES *Cookies);
extern void DpsCookiesClean(DPS_AGENT *A);

#endif
