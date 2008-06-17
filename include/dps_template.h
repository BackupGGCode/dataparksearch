/* Copyright (C) 2003 Datapark corp. All rights reserved.
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

#ifndef _DPS_TEMPLATE_H
#define _DPS_TEMPLATE_H

extern int  DpsTemplateLoad(DPS_AGENT *Agent, DPS_ENV * Env, DPS_TEMPLATE *tmplt, const char *tname);
extern __C_LINK void __DPSCALL DpsTemplatePrint(DPS_AGENT * Agent, DPS_OUTPUTFUNCTION dps_out, void *stream, char *dst, size_t dst_len, 
			     DPS_TEMPLATE *tmplt, const char *where);
extern void DpsTemplateFree(DPS_TEMPLATE *tmplt);

#endif
