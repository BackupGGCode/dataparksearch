/* Copyright (C) 2003-2012 Datapark corp. All rights reserved.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
*/

#ifndef _DPS_TEXTLIST_H
#define _DPS_TEXTLIST_H

extern __C_LINK DPS_TEXTITEM * __DPSCALL DpsTextListAdd(DPS_TEXTLIST *tlist, const DPS_TEXTITEM *item);
extern __C_LINK void __DPSCALL DpsTextListFree(DPS_TEXTLIST *tlist);
#endif
