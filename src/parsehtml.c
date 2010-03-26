/* Copyright (C) 2003-2010 Datapark corp. All rights reserved.
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
#include "dps_common.h"
#include "dps_textlist.h"
#include "dps_parsehtml.h"
#include "dps_utils.h"
#include "dps_charsetutils.h"
#include "dps_url.h"
#include "dps_match.h"
#include "dps_log.h"
#include "dps_xmalloc.h"
#include "dps_server.h"
#include "dps_hrefs.h"
#include "dps_word.h"
#include "dps_crossword.h"
#include "dps_spell.h"
#include "dps_unicode.h"
#include "dps_unidata.h"
#include "dps_uniconv.h"
#include "dps_sgml.h"
#include "dps_guesser.h"
#include "dps_vars.h"
#include "dps_mutex.h"
#include "dps_searchtool.h"
#include "dps_sea.h"
#include "dps_hash.h"
#include "dps_utils.h"
#include "dps_indexer.h"
#include "dps_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <ctype.h>

/****************************************************************/

static void DpsUniDesegment(dpsunicode_t *s) {
  register dpsunicode_t *d = s;
  while(*++s) {
    switch(*s) {
    case 0x0008:
    case 0x000A:
    case 0x000D:
    case 0x0020:
    case 0x00A0:
    case 0x1680:
    case 0x202F:
    case 0x2420:
    case 0x3000:
    case 0x303F:
    case 0xFeFF: break;
    default: if ((*s >= 0x2000) && (*s <= 0x200B)) break;
      *d = *s;
    }
  }
  *d = *s;
}


static void DpsProcessFantoms(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc, DPS_TEXTITEM *Item, size_t min_word_len, int crossec, int have_bukva_forte, 
			      dpsunicode_t *uword, int make_prefixes, int strict
#ifdef HAVE_ASPELL
		   , int have_speller, AspellSpeller *speller
#endif
			      ) {
  DPS_WORD Word;
  dpsunicode_t    *af_uword; /* Word in UNICODE with accents striped */
  dpsunicode_t    *de_uword; /* Word in UNICODE with german replaces */
  size_t  uwlen;
  int res = DPS_OK;

  TRACE_IN(Indexer, "DpsProcessFantoms");

  if (Indexer->Flags.use_accentext) {
    af_uword = DpsUniAccentStrip(uword);
    if (DpsUniStrCmp(af_uword, uword) != 0) {

      Word.uword = af_uword;
      Word.ulen = (uwlen = DpsUniLen(af_uword));

      res = DpsWordListAddFantom(Doc, &Word, Item->section);
      if (res != DPS_OK) { TRACE_OUT(Indexer); return; }
      if(Item->href && crossec){
	DPS_CROSSWORD cw;
	cw.url = Item->href;
	cw.weight = crossec;
	cw.pos = Doc->CrossWords.wordpos;
	cw.uword = af_uword;
	cw.ulen = uwlen;
	DpsCrossListAddFantom(Doc, &cw);
      }
    }
    DPS_FREE(af_uword);
    de_uword = DpsUniGermanReplace(uword);
    if (DpsUniStrCmp(de_uword, uword) != 0) {

      Word.uword = de_uword;
      Word.ulen = DpsUniLen(de_uword);

      res = DpsWordListAddFantom(Doc, &Word, Item->section);
      if (res != DPS_OK) { TRACE_OUT(Indexer); return; }
      if(Item->href && crossec){
	DPS_CROSSWORD cw;
	cw.url = Item->href;
	cw.weight = crossec;
	cw.pos = Doc->CrossWords.wordpos;
	cw.uword = de_uword;
	cw.ulen = Word.ulen;
	DpsCrossListAddFantom(Doc, &cw);
      }
    }
    DPS_FREE(de_uword);
  }

#ifdef HAVE_ASPELL
  if (have_speller && have_bukva_forte && Indexer->Flags.use_aspellext && ((uwlen = DpsUniLen(uword)) > 2)
      && (DpsUniStrChr(uword, (dpsunicode_t) '&') == NULL) /* aspell trap workaround */
      ) {
    register int ii;
    char          *utf_str = NULL, *asug = NULL;
    dpsunicode_t  *aword = NULL;
    const AspellWordList *suggestions;
    AspellStringEnumeration *elements;
    size_t tlen;

    TRACE_LINE(Indexer);
    if ((utf_str = (char*)DpsRealloc(utf_str, 16 * uwlen + 1)) == NULL) {
      TRACE_OUT(Indexer);
      return; 
    }
    if ((aword = (dpsunicode_t*)DpsMalloc((2 * uwlen + 1) * sizeof(dpsunicode_t))) == NULL) {
      DPS_FREE(utf_str); TRACE_OUT(Indexer);
      return; 
    }
    DpsConv(&Indexer->uni_utf, utf_str, 16 * uwlen, (char*)uword, (int)(sizeof(dpsunicode_t) * (uwlen + 1)));
    ii = aspell_speller_check(speller, (const char *)utf_str, (int)(tlen = dps_strlen(utf_str)));
    if ( ii == 0) {
      suggestions = aspell_speller_suggest(speller, (const char *)utf_str, (int)tlen);
      elements = aspell_word_list_elements(suggestions);
      for (ii = 0; 
	   (ii < 2) && ((asug = (char*)aspell_string_enumeration_next(elements)) != NULL);
	   ii++ ) { 

	TRACE_LINE(Indexer);
	DpsConv(&Indexer->utf_uni, (char*)aword, (2 * uwlen + 1) * sizeof(*aword), (char*)(asug), sizeof(asug[0])*(tlen + 1));
      
	Word.uword = aword;
	Word.ulen = DpsUniLen(aword);

	res = DpsWordListAddFantom(Doc, &Word, Item->section);
	if (res != DPS_OK) break;
	if(Item->href && crossec){
	  DPS_CROSSWORD cw;
	  cw.url = Item->href;
	  cw.weight = crossec;
	  cw.pos = Doc->CrossWords.wordpos;
	  cw.uword = aword;
	  cw.ulen = Word.ulen;
	  DpsCrossListAddFantom(Doc, &cw);
	}
      }
      delete_aspell_string_enumeration(elements);
    }
    DPS_FREE(utf_str); DPS_FREE(aword);
  }
#endif	

  if (!strict) {
    dpsunicode_t *dword = DpsUniDup(uword);
    dpsunicode_t *nword = NULL;
    dpsunicode_t *lt, *tok;
    size_t	tlen, nlen = 0;
    int dw_have_bukva_forte, n;

    tok = DpsUniGetToken(dword, &lt, &dw_have_bukva_forte, !strict); 
    if (tok) {
      tlen = lt - tok;
      if (tlen + 1 > nlen) {
	nword = (dpsunicode_t*)DpsRealloc(nword, (tlen + 1) * sizeof(dpsunicode_t));
	nlen = tlen;
      }
      dps_memcpy(nword, tok, tlen * sizeof(dpsunicode_t)); /* was: dps_memmove */
      nword[tlen] = 0;
      if (DpsUniStrCmp(uword, nword)) {
	for(n = 0 ;
	    tok ; 
	    tok = DpsUniGetToken(NULL, &lt, &dw_have_bukva_forte, !strict), n++ ) {

	  tlen = lt - tok;
	  if (tlen + 1 > nlen) {
	    nword = (dpsunicode_t*)DpsRealloc(nword, (tlen + 1) * sizeof(dpsunicode_t));
	    nlen = tlen;
	  }
	  dps_memcpy(nword, tok, tlen * sizeof(dpsunicode_t)); /* was: dps_memmove */
	  nword[tlen] = 0;

	  Word.uword = nword;
	  Word.ulen = DpsUniLen(nword);

	  res = DpsWordListAddFantom(Doc, &Word, Item->section);
	  if (res != DPS_OK) break;
	  if(Item->href && crossec){
	    DPS_CROSSWORD cw;
	    cw.url = Item->href;
	    cw.weight = crossec;
	    cw.pos = Doc->CrossWords.wordpos;
	    cw.uword = nword;
	    cw.ulen = Word.ulen;
	    DpsCrossListAddFantom(Doc, &cw);
	  }

	  DpsProcessFantoms(Indexer, Doc, Item, min_word_len, crossec, dw_have_bukva_forte, nword, (n) ? Indexer->Flags.make_prefixes : 0, !strict
#ifdef HAVE_ASPELL
			    , have_speller, speller
#endif
			    );
	}
      }
    }
    DPS_FREE(dword); DPS_FREE(nword);
  }

  if (make_prefixes) {
    size_t ml;
    Word.uword = uword;
    for (ml = DpsUniLen(uword) - 1; ml >= min_word_len; ml--) {
      Word.uword[ml] = 0;
      Word.ulen = ml;
      res = DpsWordListAddFantom(Doc, &Word, Item->section);
      if (res != DPS_OK) break;
    }
  }
  TRACE_OUT(Indexer);
}


int DpsPrepareItem(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc, DPS_TEXTITEM *Item, dpsunicode_t *ustr, dpsunicode_t *UStr, 
		   const char *content_lang, size_t *indexed_size, size_t *indexed_limit, 
		   size_t max_word_len, size_t min_word_len, int crossec
#ifdef HAVE_ASPELL
		   , int have_speller, AspellSpeller *speller
#endif
		   ) {
  dpsunicode_t	uspace[2] = {0x20, 0};
  DPS_VAR	*Sec;
  dpsunicode_t *nfc = NULL;
  dpsunicode_t *lt, *tok;
  dpsunicode_t    *uword = NULL;    /* Word in UNICODE      */
  char		*src;
  int have_bukva_forte, res = DPS_OK;
  size_t uwordlen = 0/*DPS_MAXWORDSIZE*/;
  size_t	srclen;
  size_t	dstlen;

  TRACE_IN(Indexer, "DpsPrepareItem");

  Sec = DpsVarListFind(&Doc->Sections, Item->section_name);
  if (Sec) {
    if (Sec->single && (Sec->val != NULL) && (Sec->curlen != 0)) { /* Already have single valued section, skip the rest from processing */
      TRACE_OUT(Indexer);
      return DPS_OK;
    }
  }
  DpsUniStrToLower(ustr);
  nfc = DpsUniNormalizeNFC(NULL, ustr);
  if (dps_need2segment(nfc)) {
    if      ((Indexer->Flags.Resegment & DPS_RESEGMENT_CHINESE)  && !strncasecmp(content_lang, "zh", 2)) DpsUniDesegment(nfc);
    else if ((Indexer->Flags.Resegment & DPS_RESEGMENT_JAPANESE) && !strncasecmp(content_lang, "ja", 2)) DpsUniDesegment(nfc);
    else if ((Indexer->Flags.Resegment & DPS_RESEGMENT_KOREAN)   && !strncasecmp(content_lang, "ko", 2)) DpsUniDesegment(nfc);
    else if ((Indexer->Flags.Resegment & DPS_RESEGMENT_THAI)     && !strncasecmp(content_lang, "th", 2)) DpsUniDesegment(nfc);
    lt = DpsUniSegment(Indexer, nfc, content_lang);
    DPS_FREE(nfc);
    nfc = lt;
  }

  TRACE_LINE(Indexer);
  if (nfc != NULL && Item->section && ((Indexer->Flags.LongestTextItems == 0) || (Item->marked)) )
    for(tok = DpsUniGetToken(nfc, &lt, &have_bukva_forte, Item->strict); 
	tok ; 
	tok = DpsUniGetToken(NULL, &lt, &have_bukva_forte, Item->strict) ) {

      size_t	tlen;				/* Word length          */ 
      DPS_WORD Word;
				
      tlen=lt-tok;
				
      if (tlen <= max_word_len && tlen >= min_word_len && (*indexed_limit == 0 || *indexed_size < *indexed_limit )) {

	*indexed_size += tlen;
				
	if (tlen > uwordlen) {
	  uwordlen = tlen;
	  if ((uword = (dpsunicode_t*)DpsRealloc(uword, 2 * (uwordlen + 1) * sizeof(dpsunicode_t))) == NULL) { 
	    TRACE_OUT(Indexer);
	    return DPS_ERROR;
	  }
	}

	dps_memcpy(uword, tok, tlen * sizeof(dpsunicode_t)); /* was: dps_memmove */
	uword[tlen]=0;

	
	Word.uword = uword;
	Word.ulen = tlen;

	res = DpsWordListAdd(Doc, &Word, Item->section);
	if(res!=DPS_OK)break;

	if(Item->href && crossec){
	  DPS_CROSSWORD cw;
	  cw.url=Item->href;
	  cw.weight = crossec;
	  cw.pos=Doc->CrossWords.wordpos;
	  cw.uword = uword;
	  cw.ulen = tlen;
	  DpsCrossListAdd(Doc, &cw);
	}

	DpsProcessFantoms(Indexer, Doc, Item, min_word_len, crossec, have_bukva_forte, uword, Indexer->Flags.make_prefixes, Item->strict
#ifdef HAVE_ASPELL
			  , have_speller, speller
#endif
			  );
			
      }
    }
  DPS_FREE(nfc);
  DPS_FREE(uword);

    if((Sec) && (strncasecmp(Item->section_name, "url.", 4) != 0) && (strcasecmp(Item->section_name, "url") != 0) ) {
      int cnvres;
			
      /* +4 to avoid attempts to fill the only one  */
      /* last byte with multibyte sequence          */
      /* as well as to add a space between portions */

      if(Sec->curlen < Sec->maxlen || Sec->maxlen == 0) {
				
	src = (char*)UStr;
	srclen = DpsUniLen(UStr) * sizeof(dpsunicode_t);
	if(!Sec->val) {
	  dstlen = dps_min(Sec->maxlen, 24 * srclen);
	  Sec->val = (char*)DpsMalloc( dstlen + 32 );
	  if (Sec->val == NULL) {
	    Sec->curlen = 0;
	    TRACE_OUT(Indexer);
	    return DPS_ERROR;
	  }
	  Sec->curlen = 0;
	} else {
	  if (Sec->maxlen) dstlen = Sec->maxlen - Sec->curlen;
	  else dstlen = 24 * srclen;
	  if ((Sec->val = DpsRealloc(Sec->val, Sec->curlen + dstlen + 32)) == NULL) {
	      Sec->curlen = 0;
	      TRACE_OUT(Indexer);
	      return DPS_ERROR;
	  }
	  /* Add space */
	  DpsConv(&Indexer->uni_lc, Sec->val + Sec->curlen, 24, (char*)(uspace), sizeof(uspace));
	  Sec->curlen += Indexer->uni_lc.obytes;
	  Sec->val[Sec->curlen] = '\0';
	}
	cnvres = DpsConv(&Indexer->uni_lc, Sec->val + Sec->curlen, dstlen, src, srclen);
	Sec->curlen += Indexer->uni_lc.obytes;
	Sec->val[Sec->curlen] = '\0';
				
	if (cnvres < 0 && Sec->maxlen) {
	  Sec->curlen = 0 /*Sec->maxlen*/;
	}
      }
    }
    TRACE_OUT(Indexer);
    return DPS_OK;
}


static int dps_itemptr_cmp(DPS_TEXTITEM **p1, DPS_TEXTITEM **p2) {
  if ((*p1)->len > (*p2)->len) return -1;
  if ((*p1)->len < (*p2)->len) return 1;
  return 0;
}


int DpsPrepareWords(DPS_AGENT * Indexer, DPS_DOCUMENT * Doc) {
  size_t		i;
  const char	*doccset;
  DPS_CHARSET	*doccs;
  DPS_CHARSET	*loccs;
  DPS_CHARSET	*sys_int;
  DPS_CONV	dc_uni;
#ifdef HAVE_LIBEXTRACTOR
  DPS_CONV      utf8_uni;
  DPS_TEXTLIST	*extractor_tlist = &Doc->ExtractorList;
#endif
  DPS_TEXTLIST	*tlist = &Doc->TextList;
  DPS_VAR	*Sec;
  int		res = DPS_OK;
  dpshash32_t	crc32 = 0;
  int		crossec, seasec;
  dpsunicode_t    *uword = NULL;    /* Word in UNICODE      */
  char            *lcsword = NULL;  /* Word in LocalCharset */
  size_t          max_word_len, min_word_len, uwordlen = DPS_MAXWORDSIZE;
  size_t          indexed_size = 0, indexed_limit = (size_t)DpsVarListFindInt(&Doc->Sections, "IndexDocSizeLimit", 0);
  const char      *content_lang = DpsVarListFindStr(&Doc->Sections, "Content-Language", "");
  const char      *SEASections = DpsVarListFindStr(&Indexer->Vars, "SEASections", "body");
  DPS_DSTR        exrpt;
#ifdef HAVE_ASPELL
  AspellCanHaveError *ret;
  AspellSpeller *speller;
  int have_speller = 0;
#endif
#ifdef WITH_PARANOIA
  void *paran = DpsViolationEnter(paran);
#endif

  TRACE_IN(Indexer, "DpsPrepareWords");
  DpsLog(Indexer, DPS_LOG_DEBUG, "Preparing words");

  if (DpsDSTRInit(&exrpt, dps_max( 4096, Doc->Buf.size >> 2 )) == NULL) {
    TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
    DpsViolationExit(Indexer->handle, paran);
#endif
    return DPS_ERROR;
  }

  if ((uword = (dpsunicode_t*)DpsMalloc((uwordlen + 1) * sizeof(dpsunicode_t))) == NULL) {
    DpsDSTRFree(&exrpt);
    TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
    DpsViolationExit(Indexer->handle, paran);
#endif
    return DPS_ERROR;
  }
  if ((lcsword = (char*)DpsMalloc(12 * uwordlen + 1)) == NULL) { DPS_FREE(uword); 
    DpsDSTRFree(&exrpt);
    TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
    DpsViolationExit(Indexer->handle, paran);
#endif
    return DPS_ERROR; 
  }

  Sec = DpsVarListFind(&Doc->Sections,"crosswords");
  crossec = Sec ? Sec->section : 0;
  Sec = DpsVarListFind(&Doc->Sections, "sea");
  seasec = Sec ? Sec->section : 0;
	
  doccset=DpsVarListFindStr(&Doc->Sections,"Charset",NULL);
  if(!doccset||!*doccset)doccset=DpsVarListFindStr(&Doc->Sections,"RemoteCharset","iso-8859-1");
  doccs=DpsGetCharSet(doccset);
  if(!doccs)doccs=DpsGetCharSet("iso-8859-1");
  loccs = Doc->lcs;
  if (!loccs) loccs = Indexer->Conf->lcs;
  if (!loccs) loccs = DpsGetCharSet("iso-8859-1");
  sys_int=DpsGetCharSet("sys-int");

  DpsConvInit(&dc_uni, loccs /*doccs*/, sys_int, Indexer->Conf->CharsToEscape, DPS_RECODE_HTML);
#ifdef HAVE_LIBEXTRACTOR
  DpsConvInit(&utf8_uni, DpsGetCharSet("utf-8"), sys_int, Indexer->Conf->CharsToEscape, DPS_RECODE_HTML);
#endif
  max_word_len = Indexer->WordParam.max_word_len;
  min_word_len = Indexer->WordParam.min_word_len;
	
#ifdef HAVE_ASPELL
  if (Indexer->Flags.use_aspellext) {
    aspell_config_replace(Indexer->aspell_config, "lang", (*content_lang != '\0') ? content_lang : "en");
    ret = new_aspell_speller(Indexer->aspell_config);
    if (aspell_error(ret) != 0) {
      DpsLog(Indexer, DPS_LOG_ERROR, " aspell error: %s", aspell_error_message(ret));
      delete_aspell_can_have_error(ret);
    } else {
      speller = to_aspell_speller(ret);
      have_speller = 1;
    }
  }
#endif				
	
  if (Indexer->Flags.LongestTextItems > 0) {
    DPS_TEXTITEM **items = (DPS_TEXTITEM**)DpsMalloc((tlist->nitems + 1) * sizeof(DPS_TEXTITEM));
    if (items != NULL) {
      for(i = 0; i < tlist->nitems; i++) items[i] = &tlist->Items[i];
      DpsSort(items, tlist->nitems, sizeof(DPS_TEXTITEM*), (qsort_cmp) dps_itemptr_cmp);
      for(i = 0; (i < tlist->nitems) && (i < (size_t)Indexer->Flags.LongestTextItems); i++) items[i]->marked = 1;
    }
    DPS_FREE(items);
  }

/* Now convert everything to UNICODE format and calculate CRC32 */

  for(i = 0; i < tlist->nitems; i++) {
    size_t		srclen;
    size_t		dstlen;
    size_t		reslen;
    char		*src,*dst;
    dpsunicode_t	*ustr = NULL, *UStr = NULL;
    DPS_TEXTITEM	*Item = &tlist->Items[i];
		
		
    srclen = ((Item->len) ? Item->len : (dps_strlen(Item->str)) + 1);	/* with '\0' */
    dstlen = (16 * (srclen + 1)) * sizeof(dpsunicode_t);	/* with '\0' */
		
    if ((ustr = (dpsunicode_t*)DpsMalloc(dstlen + 1)) == NULL) {
      DpsLog(Indexer, DPS_LOG_ERROR, "%s:%d Can't alloc %u bytes", __FILE__, __LINE__, dstlen);
      DPS_FREE(uword); DPS_FREE(lcsword); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
		
    src = Item->str;
    dst = (char*)ustr;

    DpsConv(&dc_uni, dst, dstlen, src, srclen);
/*		DpsSGMLUniUnescape(ustr);*/
    DpsUniRemoveDoubleSpaces(ustr);
    if ((UStr = DpsUniDup(ustr)) == NULL) {
      DpsLog(Indexer, DPS_LOG_ERROR, "%s:%d Can't DpsUniDup", __FILE__, __LINE__);
      DPS_FREE(uword); DPS_FREE(lcsword); DPS_FREE(ustr); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
    reslen = DpsUniLen(ustr);
		
    /*
      TODO for clones detection:
      Replace any separators into space to ignore 
      various pseudo-graphics, commas, semicolons
      and so on to improve clone detection quality
    */
    if (strncasecmp(DPS_NULL2EMPTY(Item->section_name), "url", 3) != 0) /* do not calculate crc32  on url* sections */
      crc32 = DpsHash32Update(crc32, (char*)ustr, reslen * sizeof(dpsunicode_t));

    /* Collect links from HrefSections */
    if((Sec = DpsVarListFind(&Indexer->Conf->HrefSections, Item->section_name))) {
      DPS_HREF	Href;
      DpsHrefInit(&Href);
      Href.referrer = DpsVarListFindInt(&Doc->Sections, "Referrer-ID", 0);
      Href.hops = 1 + DpsVarListFindInt(&Doc->Sections, "Hops", 0);
      Href.site_id = 0; /*DpsVarListFindInt(&Doc->Sections, "Site_id", 0);*/
      Href.url = Item->str;
      Href.method = DPS_METHOD_GET;
      DpsHrefListAdd(Indexer, &Doc->Hrefs, &Href);
    }


    if (seasec && strstr(SEASections, Item->section_name)) {
      DpsDSTRAppendUniWithSpace(&exrpt, UStr);
    }

    if (DPS_OK != DpsPrepareItem(Indexer, Doc, Item, ustr, UStr, 
				 content_lang, &indexed_size, &indexed_limit, max_word_len, min_word_len, crossec
#ifdef HAVE_ASPELL
				 , have_speller, speller
#endif
				 )) {
      DPS_FREE(uword); DPS_FREE(lcsword); DPS_FREE(ustr); DPS_FREE(UStr); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
		
		
    DPS_FREE(ustr);
    DPS_FREE(UStr);
    if (res != DPS_OK) break;
  }

 /* do the same for libextrated meta-data, which is always encoded in UTF-8 */
#ifdef HAVE_LIBEXTRACTOR
  for(i = 0; i < extractor_tlist->nitems; i++) {
    size_t		srclen;
    size_t		dstlen;
    size_t		reslen;
    char		*src,*dst;
    dpsunicode_t	*ustr = NULL, *UStr = NULL;
    DPS_TEXTITEM	*Item = &extractor_tlist->Items[i];

    srclen = ((Item->len) ? Item->len : (dps_strlen(Item->str)) + 1);	/* with '\0' */
    dstlen = (16 * (srclen + 1)) * sizeof(dpsunicode_t);	/* with '\0' */
		
    if ((ustr = (dpsunicode_t*)DpsMalloc(dstlen + 1)) == NULL) {
      DpsLog(Indexer, DPS_LOG_ERROR, "%s:%d Can't alloc %u bytes", __FILE__, __LINE__, dstlen);
      DPS_FREE(uword); DPS_FREE(lcsword); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
		
    src = Item->str;
    dst = (char*)ustr;

    DpsConv(&utf8_uni, dst, dstlen, src, srclen);
    DpsUniRemoveDoubleSpaces(ustr);
    if ((UStr = DpsUniDup(ustr)) == NULL) {
      DpsLog(Indexer, DPS_LOG_ERROR, "%s:%d Can't DpsUniDup", __FILE__, __LINE__);
      DPS_FREE(uword); DPS_FREE(lcsword); DPS_FREE(ustr); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
    reslen = DpsUniLen(ustr);
		
    /*
      TODO for clones detection:
      Replace any separators into space to ignore 
      various pseudo-graphics, commas, semicolons
      and so on to improve clone detection quality
    */
    if (strncasecmp(DPS_NULL2EMPTY(Item->section_name), "url", 3) != 0) /* do not calculate crc32  on url* sections */
      crc32 = DpsHash32Update(crc32, (char*)ustr, reslen * sizeof(dpsunicode_t));

    /* Collect links from HrefSections */
    if((Sec = DpsVarListFind(&Indexer->Conf->HrefSections, Item->section_name))) {
      DPS_HREF	Href;
      DpsHrefInit(&Href);
      Href.referrer = DpsVarListFindInt(&Doc->Sections, "Referrer-ID", 0);
      Href.hops = 1 + DpsVarListFindInt(&Doc->Sections, "Hops", 0);
      Href.site_id = 0; /*DpsVarListFindInt(&Doc->Sections, "Site_id", 0);*/
      Href.url = Item->str;
      Href.method = DPS_METHOD_GET;
      DpsHrefListAdd(Indexer, &Doc->Hrefs, &Href);
    }

    if (seasec && strstr(SEASections, Item->section_name)) {
      DpsDSTRAppendUniWithSpace(&exrpt, UStr);
    }

    if (DPS_OK != DpsPrepareItem(Indexer, Doc, Item, ustr, UStr, 
				 content_lang, &indexed_size, &indexed_limit, max_word_len, min_word_len, crossec
#ifdef HAVE_ASPELL
				 , have_speller, speller
#endif
				 )) {
      DPS_FREE(uword); DPS_FREE(lcsword); DPS_FREE(ustr); DPS_FREE(UStr); 
      DpsDSTRFree(&exrpt);
      TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
      DpsViolationExit(Indexer->handle, paran);
#endif
      return DPS_ERROR;
    }
		
		
    DPS_FREE(ustr);
    DPS_FREE(UStr);
    if (res != DPS_OK) break;
  }
#endif

  DpsVarListReplaceInt(&Doc->Sections,"crc32", (int)crc32);

  if (seasec) {
    DpsSEAMake(Indexer, Doc, &exrpt, content_lang, &indexed_size, &indexed_limit, max_word_len, min_word_len, crossec, seasec
#ifdef HAVE_ASPELL
	       , have_speller, speller
#endif
	       );
  }

#ifdef HAVE_ASPELL
  if (have_speller && Indexer->Flags.use_aspellext) {
    delete_aspell_speller(speller);
  }
#endif
	
  DPS_FREE(uword); DPS_FREE(lcsword); 
  DpsDSTRFree(&exrpt);
  TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
  DpsViolationExit(Indexer->handle, paran);
#endif
  return res;
}


/**************************** Built-in Parsers ***************************/

int DpsParseURLText(DPS_AGENT *A, DPS_DOCUMENT *Doc) {
	DPS_TEXTITEM	Item;
	DPS_URL         dcURL;
	DPS_CONV        lc_dc;
	DPS_VAR		*Sec;
	DPS_CHARSET	*doccs, *loccs;
	const char      *lc_url = DpsVarListFindStr(&Doc->Sections, "E_URL", NULL);
	char            *dc_url;
	size_t          len;


	if (lc_url == NULL) {
	  lc_url = DpsVarListFindStr(&Doc->Sections, "URL", "");
	}
	len = dps_strlen(lc_url);
	dc_url = (char*)DpsMalloc(24 * len + sizeof(dpsunicode_t));
	if (dc_url == NULL) return DPS_ERROR;

	loccs = A->Conf->lcs;
	if(!loccs) loccs = DpsGetCharSet("iso-8859-1");
	doccs = DpsGetCharSetByID(Doc->charset_id);
	if(!doccs) doccs = DpsGetCharSet("iso-8859-1");
	DpsConvInit(&lc_dc, loccs, doccs, A->Conf->CharsToEscape, DPS_RECODE_URL_FROM);
	DpsConv(&lc_dc, dc_url, (size_t)24 * len,  lc_url, (size_t)(len + 1));

	dcURL.freeme = 0;
	DpsURLInit(&dcURL);
	if (DpsURLParse(&dcURL, dc_url)) { DPS_FREE(dc_url); return DPS_ERROR; }
	
	bzero((void*)&Item, sizeof(Item));
	Item.href = NULL;
	
	if((Sec = DpsVarListFind(&Doc->Sections, "url"))) {
		char sc[] = "url\0";
		Item.str = DPS_NULL2EMPTY(dc_url);
		Item.section = Sec->section;
		Item.strict = Sec->strict;
		Item.section_name = sc;
		Item.len = 0;
		DpsTextListAdd(&Doc->TextList, &Item);
	}
	if((Sec=DpsVarListFind(&Doc->Sections,"url.proto"))) {
		char sc[] = "url.proto\0";
		Item.str = DPS_NULL2EMPTY(dcURL.schema);
		Item.section = Sec->section;
		Item.strict = Sec->strict;
		Item.section_name = sc;
		Item.len = 0;
		DpsTextListAdd(&Doc->TextList, &Item);
		DpsVarListReplaceStr(&Doc->Sections, "url.proto", Item.str);
	}
	if((Sec=DpsVarListFind(&Doc->Sections,"url.host"))) {
		char sc[] = "url.host\0";
		Item.str = DPS_NULL2EMPTY(dcURL.hostname);
		Item.section = Sec->section;
		Item.strict = Sec->strict;
		Item.section_name = sc;
		Item.len = 0;
		DpsTextListAdd(&Doc->TextList, &Item);
		DpsVarListReplaceStr(&Doc->Sections, "url.host", Item.str);
	}
	if((Sec=DpsVarListFind(&Doc->Sections,"url.path"))) {
		char sc[] = "url.path\0";
		Item.str = DPS_NULL2EMPTY(dcURL.path);
		Item.section = Sec->section;
		Item.strict = Sec->strict;
		Item.section_name = sc;
		Item.len = 0;
		DpsTextListAdd(&Doc->TextList, &Item);
		DpsVarListReplaceStr(&Doc->Sections, "url.path", Item.str);
	}
	if((Sec=DpsVarListFind(&Doc->Sections,"url.directory"))) {
		char sc[]="url.directory\0";
		Item.str = DPS_NULL2EMPTY(dcURL.directory);
		Item.section = Sec->section;
		Item.strict = Sec->strict;
		Item.section_name = sc;
		Item.len = 0;
		DpsTextListAdd(&Doc->TextList, &Item);
		DpsVarListReplaceStr(&Doc->Sections, "url.directory", Item.str);
	}
	if((Sec=DpsVarListFind(&Doc->Sections,"url.file"))) {
	        char *str, sc[]="url.file\0";
		size_t len;
		str = (char*)DpsMalloc((len = dps_strlen(DPS_NULL2EMPTY(dcURL.filename))) + 1);
		if (str != NULL) {
		  DpsUnescapeCGIQuery(str, DPS_NULL2EMPTY(dcURL.filename));
		  Item.str = str;
		  Item.section = Sec->section;
		  Item.strict = Sec->strict;
		  Item.section_name = sc;
		  Item.len = len;
		  DpsTextListAdd(&Doc->TextList, &Item);
		  DpsVarListReplaceStr(&Doc->Sections, "url.file", Item.str);
		  DPS_FREE(str);
		}
	}
	DpsURLFree(&dcURL);
	DPS_FREE(dc_url);
	return DPS_OK;
}
/*
int DpsParseHeaders(DPS_AGENT *Indexer,DPS_DOCUMENT *Doc){
	size_t i;
	DPS_TEXTITEM Item;
	
	bzero((void*)&Item, sizeof(Item));
	Item.href=NULL;
	for(i=0;i<Doc->Sections.nvars;i++){
		char	secname[128];
		DPS_VAR	*Sec;
		dps_snprintf(secname,sizeof(secname),"header.%s",Doc->Sections.Var[i].name);
		secname[sizeof(secname)-1]='\0';
		if((Sec=DpsVarListFind(&Doc->Sections,secname))){
			Item.str=Doc->Sections.Var[i].val;
			Item.section = Sec->section;
			Item.strict = Sec->strict;
			Item.section_name=secname;
			DpsTextListAdd(&Doc->TextList,&Item);
		}
	}
	return DPS_OK;
}
*/
int DpsParseText(DPS_AGENT * Indexer,DPS_DOCUMENT * Doc){
	DPS_TEXTITEM	Item;
	DPS_VAR		*BSec=DpsVarListFind(&Doc->Sections,"body");
	char            *buf_content = (Doc->Buf.pattern == NULL) ? Doc->Buf.content : Doc->Buf.pattern;
	char savec;

	DpsLog(Indexer, DPS_LOG_DEBUG, "Executing Text parser");
	
	if (BSec == NULL) return DPS_OK;
	Item.href = NULL;
	
	if(BSec && buf_content && Doc->Spider.index){
		char *lt;
                bzero((void*)&Item, sizeof(Item));
		Item.section = BSec->section;
		Item.strict = BSec->strict;
		Item.str = dps_strtok_r(buf_content, "\r\n", &lt, &savec);
		Item.section_name = BSec->name; /*"body";*/
		while(Item.str){
		        Item.len = (lt != NULL) ? (lt - Item.str) : dps_strlen(Item.str);
			DpsTextListAdd(&Doc->TextList, &Item);
			Item.str = dps_strtok_r(NULL, "\r\n", &lt, &savec);
		}
	}
	return(DPS_OK);
}


static void DpsNextCharB(void *d) {
  DPS_HTMLTOK *t = (DPS_HTMLTOK *)d;
  (t->b)++;
}

static void DpsNextCharE(void *d) {
  DPS_HTMLTOK *t = (DPS_HTMLTOK *)d;
  (t->e)++;
}


void DpsHTMLTOKInit(DPS_HTMLTOK *tag) {
  bzero((void*)tag, sizeof(*tag));
  tag->next_b = &DpsNextCharB;
  tag->next_e = &DpsNextCharE;
  tag->trailend = tag->trail;
  tag->visible[0] = 1;
}

void DpsHTMLTOKFree(DPS_HTMLTOK *tag) {
/*  register size_t i;
  for (i = 0; i < sizeof(tag->section); i++) DPS_FREE(tag->section_name[i]);*/
}


const char * DpsHTMLToken(const char * s, const char ** lt,DPS_HTMLTOK *t){

	t->ntoks=0;
	t->s = s;
	t->lt = lt;
	
	if(t->s == NULL && (t->s = *lt) == NULL)
		return NULL;

	if(!*t->s) return NULL;
	
	if(!strncmp(t->s,"<!--",4)) t->type = DPS_HTML_COM;
	else	
	if(*t->s=='<' && t->s[1] != ' ' && t->s[1] != '<' && t->s[1] != '>') t->type = DPS_HTML_TAG;
	else	t->type = DPS_HTML_TXT;

	switch(t->type){
		case DPS_HTML_TAG:

			for(*lt = t->b = t->s + 1; *t->b; ) {
				const char * valbeg=NULL;
				const char * valend=NULL;
				size_t nt=t->ntoks;
				
				
				/* Skip leading spaces */
				while((*t->b)&&strchr(" \t\r\n",*t->b)) (*t->next_b)(t);

				if(*t->b=='>'){
					*lt = t->b + 1;
					return(t->s);
				}

				if(*t->b=='<'){ /* Probably broken tag occure */
					*lt = t->b;
					return(t->s);
				}

				/* Skip non-spaces, i.e. name */
				for(t->e = t->b; (*t->e) && !strchr(" =>\t\r\n", *t->e); (*t->next_e)(t));
				
				if(t->ntoks<DPS_MAXTAGVAL)
					t->ntoks++;
				
				t->toks[nt].val=0;
				t->toks[nt].vlen=0;
				t->toks[nt].name = t->b;
				t->toks[nt].nlen = t->e - t->b;

				if (nt == 0) {
				  if(!strncasecmp(t->b,"script",6)) t->script = 1;
				  else if(!strncasecmp(t->b,"/script",7)) t->script = 0;
				  else if(!strncasecmp(t->b, "style", 5)) t->style = 1;
				  else if(!strncasecmp(t->b, "/style", 6)) t->style = 0;
				  else if(!strncasecmp(t->b, "select", 4)) t->select = 1;
				  else if(!strncasecmp(t->b, "/select", 5)) t->select = 0;
				  else if(!strncasecmp(t->b, "body", 4)) t->body = 1;
				  else if(!strncasecmp(t->b, "/body", 5)) t->body = 0;
				  else if(!strncasecmp(t->b, "noindex", 7)) t->comment = 1;
				  else if(!strncasecmp(t->b, "/noindex", 8)) t->comment = 0;
				}

				if(*t->e=='>'){
					*lt = t->e + 1;
					return(t->s);
				}

				if(!(*t->e)){
					*lt = t->e;
					return(t->s);
				}
				
				/* Skip spaces */
				while((*t->e) && strchr(" \t\r\n",*t->e))(*t->next_e)(t);
				
				if(*t->e != '='){
					t->b = t->e;
				       *lt = t->b;        /* bug when hang on broken inside tag pages fix */
					continue;
				}
				
				/* Skip spaces */
				for(t->b = t->e + 1; (*t->b) && strchr(" \r\n\t", *t->b); (*t->next_b)(t));
				
				if(*t->b == '"'){
					t->b++;
					
					valbeg = t->b;
					for(t->e = t->b; (*t->e) && (*t->e != '"'); (*t->next_e)(t));
					valend = t->e;
					
					t->b = t->e;
					if(*t->b == '"')(*t->next_b)(t);
				}else
				if(*t->b == '\''){
					t->b++;
					
					valbeg = t->b;
					for(t->e = t->b; (*t->e) && (*t->e != '\''); (*t->next_e)(t));
					valend = t->e;
					
					t->b = t->e;
					if(*t->b == '\'')(*t->next_b)(t);
				}else{
					valbeg = t->b;
					for(t->e = t->b; (*t->e) && !strchr(" >\t\r\n", *t->e);(*t->next_e)(t));
					valend = t->e;
					
					t->b = t->e;
				}
				*lt = t->b;
				t->toks[nt].val=valbeg;
				t->toks[nt].vlen=valend-valbeg;
			}
			break;

		case DPS_HTML_COM: /* comment */
			
			if(!strncasecmp(t->s, "<!--DpsComment-->", 17))	t->comment=1;
			else
			if(!strncasecmp(t->s, "<!--/DpsComment-->", 18)) t->comment=0;
			else
			if(!strncasecmp(t->s, "<!--UdmComment-->", 17)) t->comment=1;
			else
			if(!strncasecmp(t->s, "<!--/UdmComment-->", 18)) t->comment=0;
			else
			if(!strncasecmp(t->s, "<!--noindex-->", 14)) t->comment=1;
			else
			if(!strncasecmp(t->s, "<!--/noindex-->", 15)) t->comment=0;
			else
			if(!strncasecmp(t->s, "<!--Comment-->", 14)) t->comment=1;
			else
			if(!strncasecmp(t->s, "<!--/Comment-->", 15)) t->comment=0;
			else
			if(!strncasecmp(t->s, "<!--htdig_noindex-->", 20)) t->comment=1;
			else
			if(!strncasecmp(t->s, "<!--/htdig_noindex-->", 21)) t->comment=0;
			else
			if(!strncasecmp(t->s, "<!-- google_ad_section_start -->", 32)) t->comment = 0;
			else
			if(!strncasecmp(t->s, "<!-- google_ad_section_start(weight=ignore) -->", 47)) t->comment = 1;
			else
			if(!strncasecmp(t->s, "<!-- google_ad_section_end -->", 30)) t->comment = !(t->comment);

			for(t->e = t->s; (*t->e) && (strncmp(t->e, "-->", 3)); (*t->next_e)(t));
			if(!strncmp(t->e, "-->", 3)) *lt = t->e + 3;
			else	*lt = t->e;
			break;

		case DPS_HTML_TXT: /* text */
		default:
			/* Special case when script  */
			/* body is not commented:    */
			/* <script> x="<"; </script> */
			/* We should find </script>  */
			
			for(t->e = t->s; *t->e; (*t->next_e)(t)){
				if(*t->e == '<'){
					if(t->script){
						if(!strncasecmp(t->e, "</script>",9)){
							/* This is when script body  */
							/* is not hidden using <!--  */
							break;
						}else
						if(!strncmp(t->e, "<!--",4)){
							/* This is when script body  */
							/* is hidden but there are   */
							/* several spaces between    */
							/* <SCRIPT> and <!--         */
							break;
						}
					}else{
					  if (t->e == t->s) continue;
						break;
					}
				}
			}
			
			*lt = t->e;
			break;
	}
	return t->s;
}


int DpsHTMLParseTag(DPS_AGENT *Indexer, DPS_HTMLTOK * tag, DPS_DOCUMENT * Doc) {
	DPS_TEXTITEM Item;
	DPS_VAR	*Sec;
	int opening, visible = 0;
	char name[128];
	register char * n;
	char *metaname = NULL;
	char *metacont = NULL;
	char *href = NULL;
	char *base = NULL;
	char *lang = NULL;
	char *secname = NULL;
	size_t i, seclen = 128, metacont_len = 0;
	char savec;

#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif

	if(!tag->ntoks) {
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return(0);
	}
	if(!tag->toks[0].name) {
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return(0);
	}
	if(tag->toks[0].nlen>sizeof(name)-1) {
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return(0);
	}

	secname = (char*)DpsMalloc(seclen + 1);
	if (secname == NULL) {
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return(0);
	}
	dps_strncpy(name, tag->toks[0].name, (i = dps_min(sizeof(name) - 1, tag->toks[0].nlen)) );
	name[i]='\0';
	
	for(n=name; *n; *n = dps_tolower(*n), n++);
	
	if(name[0]=='/'){
	        char *e;
		size_t glen, slen;
		opening = 0;
		dps_memcpy(name, name + 1, (slen = dps_strlen(name+1)) + 1); /* was: dps_memmove */
		if ((strcasecmp(name, "p")) && (strcasecmp(name, "br")) && (strcasecmp(name, "option")) && (strcasecmp(name, "input")) && (strcasecmp(name, "font"))) {
		  do {
		    /* Find previous '.' or beginning */
		    for(e = tag->trailend; (e > tag->trail) && (e[0] != '.'); e--);
		    glen = (e[0] == '.') ? (tag->trailend - e - 1) : tag->trailend - e;
		    *e = '\0';
		    tag->trailend = e;
		    if (tag->level) { tag->level--; tag->section[tag->level] = 0; }
		  } while ((strcmp(e + 1, name) != 0) && tag->trailend > tag->trail);
		}
	}else{
	        size_t name_len = dps_strlen(name);
		opening = 1;
		if ((strcasecmp(name, "p")) && (strcasecmp(name, "br")) && (strcasecmp(name, "option")) && (strcasecmp(name, "input")) && (strcasecmp(name, "font"))) {
		  Sec = DpsVarListFind(&Doc->Sections, name);
		  if (tag->level < sizeof(tag->visible) - 1) visible = tag->visible[tag->level + 1] = tag->visible[tag->level];
		  tag->section[tag->level] = (Sec) ? Sec->section : 0;
		  tag->strict[tag->level] = (Sec) ? Sec->strict : 0;
		  tag->section_name[tag->level] = (Sec) ? Sec->name : NULL;
		  if ((tag->level + 2 >= sizeof(tag->visible)) || (((tag->trailend - tag->trail) + name_len + 2) > sizeof(tag->trail)) ) {
		    DpsLog(Indexer, DPS_LOG_WARN, "Too deep or incorrect HTML, level:%d, trailsize:%d", tag->level, (tag->trailend - tag->trail) + name_len);
		    DpsLog(Indexer, DPS_LOG_DEBUG, " -- trail: %s", tag->trail);
#ifdef WITH_PARANOIA
		    DpsViolationExit(Indexer->handle, paran);
#endif
		    return 0;
		  }
		  tag->level++;
		  if (tag->trailend > tag->trail) {
		    tag->trailend[0] = '.';
		    tag->trailend++;
		  }
		  dps_memmove(tag->trailend, name, name_len);
		  tag->trailend += name_len;
		  tag->trailend[0] = '\0';
		}
	}

	tag->follow = Doc->Spider.follow;
	tag->index = Doc->Spider.index;

	for(i=0;i<tag->ntoks;i++){
		if(ISTAG(i,"name")){
		  DPS_FREE(metaname);
		  if (tag->toks[i].vlen) metaname = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		}else
		if(ISTAG(i,"http-equiv")){
		  DPS_FREE(metaname);
		  if (tag->toks[i].vlen) metaname = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		}else
		if(ISTAG(i,"content")){
		  DPS_FREE(metacont);
		  if (tag->toks[i].vlen) metacont = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  metacont_len = tag->toks[i].vlen;
		}else
		if(ISTAG(i,"href")){
		  /* A, LINK, AREA*/
		  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  DPS_FREE(href);
		  href = (char*)DpsStrdup(DpsTrim(y, " \t\r\n"));
		  DPS_FREE(y);
		}else
		if(ISTAG(i,"base")) {
		  /* OBJECT, EMBED*/
		  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  DPS_FREE(base);
		  base = (char*)DpsStrdup(DpsTrim(y, " \t\r\n"));
		  DPS_FREE(y);
		}else
		if(Indexer->Flags.rel_nofollow && ISTAG(i, "rel")) {
		  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  if (strcasestr(y, "nofollow") != NULL) {
		    tag->follow = DPS_FOLLOW_NO;
		  }
		  DPS_FREE(y);
		}else
		if(ISTAG(i, "style")) {
		  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  char *z = strcasestr(y, "visibility:");
		  if (z != NULL) {
		    char *p = strchr(z, (int)';');
		    char *x = strcasestr(z, "visible");
		    visible = tag->visible[tag->level];
		    if (x && ((p == NULL) || (p > x))) {
		      visible = 1;
		    }
		    x = strcasestr(z, "hidden");
		    if (x && ((p == NULL) || (p > x))) {
		      visible = 0;
		    }
		    x = strcasestr(z, "none");
		    if (x && ((p == NULL) || (p > x))) {
		      visible = 0;
		    }
		    tag->visible[tag->level] = visible;
		  }
		  z = strcasestr(y, "display:");
		  if (z != NULL) {
		    char *p = strchr(z, (int)';');
		    char *x = strcasestr(z, "none");
		    visible = tag->visible[tag->level];
		    if (x && ((p == NULL) || (p > x))) {
		      visible = 0;
		    } else visible = 1;
		    tag->visible[tag->level] = visible;
		  }
		  DPS_FREE(y);
		}else
		if(ISTAG(i, "src")) {
			/* IMG, FRAME, IFRAME, EMBED */
		  if (href == NULL) {
		    char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		    href = (char*)DpsStrdup(DpsTrim(y, " \t\r\n"));
		    DPS_FREE(y);
		  }
		}else
		if (ISTAG(i, "lang")) {
		  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
		  DPS_FREE(lang);
		  lang = (char*)DpsStrdup(DpsTrim(y, " \t\r\n"));
		  for(n = lang; *n; *n = dps_tolower(*n),n++);
		  DPS_FREE(y);
		} /*else*/ {
		        if (tag->toks[i].nlen + 12 > seclen) {
			  secname = (char*)DpsRealloc(secname, seclen = (tag->toks[i].nlen + 12));
			  if (secname == NULL) {
#ifdef WITH_PARANOIA
			    DpsViolationExit(Indexer->handle, paran);
#endif
			    return(0);
			  }
			}
			
		        dps_strcpy(secname, "attribute.");
			dps_strncat(secname + 10, tag->toks[i].name, tag->toks[i].nlen);
			secname[seclen - 1]='\0';

			if ((Sec = DpsVarListFind(&Doc->Sections, secname)) && Doc->Spider.index && visible) {
			  char *y = DpsStrndup(DPS_NULL2EMPTY(tag->toks[i].val), tag->toks[i].vlen);
			  bzero((void*)&Item, sizeof(Item));
			  Item.str = y;
			  Item.section = Sec->section;
			  Item.strict = Sec->strict;
			  Item.section_name = Sec->name;
			  Item.href = NULL;
			  Item.len = tag->toks[i].vlen;
			  DpsTextListAdd(&Doc->TextList, &Item);
			  DPS_FREE(y);
			}
		}
	}
	
	/* Let's find tag name in order of frequency */

	if(!strcmp(name,"a")){
		DPS_FREE(tag->lasthref);			/*117941*/
	}else
	if(!strcmp(name,"title"))	tag->title=opening;	/*6192*/
	else
	if(!strcmp(name,"html") && opening && (lang != NULL)) {
		DpsVarListReplaceStr(&Doc->Sections, "Meta-Language", lang);
	}else
	if(!strcmp(name,"body")) {
		tag->body=opening;	/*5146*/
		if (opening && (lang != NULL)) {
			DpsVarListReplaceStr(&Doc->Sections, "Meta-Language", lang);
		}
	}else
	if((!strcmp(name,"meta"))&&(metaname)&&(metacont)){ 
		
		dps_strcpy(secname,"meta.");
		dps_strncat(secname + 5 ,metaname, seclen - 5);
		secname[seclen - 1]='\0';
		
		if((!tag->comment) && (Sec=DpsVarListFind(&Doc->Sections,secname)) && Doc->Spider.index && visible) {
/*			DpsSGMLUnescape(metacont);   we do this later */
		        bzero((void*)&Item, sizeof(Item));
			Item.str=metacont;
			Item.section = Sec->section;
			Item.strict = Sec->strict;
			Item.section_name=secname;
			Item.href = NULL;
			Item.len = metacont_len;
			DpsTextListAdd(&Doc->TextList,&Item);
		}
		
		if(!strcasecmp(metaname,"Content-Type")){
			char *p;
			if((p = strcasestr(metacont, "charset="))) {
				const char *cs = DpsCharsetCanonicalName(DpsTrim(p + 8, " \t;\"'"));
				DpsVarListReplaceStr(&Doc->Sections, "Meta-Charset", cs ? cs : p + 8);
			}
		}else
		if(!strcasecmp(metaname, "Content-Language") || !strcasecmp(metaname, "DC.Language")) {
			char *l;
			l = (char*)DpsStrdup(metacont);
			for(n = l; *n; *n = dps_tolower(*n), n++);
/*			if (dps_strlen(l) > 2) {
			  if (l[2] != '_') {
			    l[2] = '\0';
			  } else {
			    if (dps_strlen(l) > 5) {
			      l[5] = '\0';
			    }
			  }
			}*/
			DpsVarListReplaceStr(&Doc->Sections, "Meta-Language", l);
			DPS_FREE(l);
		}else
		if(!strcasecmp(metaname,"refresh")){
			/* Format: "10; Url=http://something/" */
			/* URL can be written in different     */
			/* forms: URL, url, Url and so on      */
		        char *p;
			
			if((p = strchr(metacont, '='))){
				if((p >= metacont + 3) && (!strncasecmp(p-3,"URL=",4))){
				  DPS_FREE(href);
				  href = (char*)DpsStrdup(p + 1);
				}
				/* noindex if redirect on refresh */
				tag->index = 0;
				Doc->Spider.index = 0;
			}
 		}else
		if(!strcasecmp(metaname,"robots")&&(Doc->Spider.use_robots)&&(metacont)){
			char * lt;
			char * rtok;
					
			rtok = dps_strtok_r(metacont, " ,\r\n\t", &lt, &savec);
			while(rtok){
				if(!strcasecmp(rtok,"ALL")){
					/* Set Server parameters */
					tag->follow=Doc->Spider.follow;
					tag->index=Doc->Spider.index;
				}else if(!strcasecmp(rtok,"NONE")){
					tag->follow=DPS_FOLLOW_NO;
					tag->index=0;
					Doc->Spider.follow = DPS_FOLLOW_NO;
					Doc->Spider.index = 0;
					if (DpsNeedLog(DPS_LOG_DEBUG)) {
					  DpsVarListReplaceInt(&Doc->Sections, "Index", 0);
					  DpsVarListReplaceInt(&Doc->Sections, "Follow", DPS_FOLLOW_NO);
					}
				}else if(!strcasecmp(rtok,"NOINDEX")) {
					tag->index=0;
					Doc->Spider.index = 0;
/*					Doc->method = DPS_METHOD_DISALLOW;*/
					if (DpsNeedLog(DPS_LOG_DEBUG)) DpsVarListReplaceInt(&Doc->Sections, "Index", 0);
				}else if(!strcasecmp(rtok,"NOFOLLOW")) {
					tag->follow=DPS_FOLLOW_NO;
					Doc->Spider.follow = DPS_FOLLOW_NO;
					if (DpsNeedLog(DPS_LOG_DEBUG)) DpsVarListReplaceInt(&Doc->Sections, "Follow", DPS_FOLLOW_NO);
				}else if(!strcasecmp(rtok,"NOARCHIVE")) {
				        DpsVarListReplaceStr(&Doc->Sections, "Z", "");
				}else if(!strcasecmp(rtok,"INDEX")) {
				        tag->index = Doc->Spider.index;
					if (DpsNeedLog(DPS_LOG_DEBUG)) DpsVarListReplaceInt(&Doc->Sections, "Index", Doc->Spider.index);
				}else if(!strcasecmp(rtok,"FOLLOW")) {
					tag->follow=Doc->Spider.follow;
					if (DpsNeedLog(DPS_LOG_DEBUG)) DpsVarListReplaceInt(&Doc->Sections, "Follow", Doc->Spider.follow);
				}
				rtok = dps_strtok_r(NULL, " \r\n\t", &lt, &savec);
			}
		}else
		if(!strcasecmp(metaname, "DP.PopRank")) {
		  char pstr[32];
		  double pop_rank = dps_atof(metacont);
		  dps_snprintf(pstr, sizeof(pstr), "%f", pop_rank);
		  DpsVarListReplaceStr(&Doc->Sections, "Pop_Rank", pstr);
		}else
		if(!strcasecmp(metaname, "Last-Modified")) {
		  DpsVarListReplaceStr(&Doc->Sections, metaname, metacont);
		}else
		if(!strcasecmp(metaname, "geo.position")) {
		  double lat, lon;
		  char *l = strchr(metacont, (int)';');
		  if (l != NULL) {
		    lat = dps_atof(metacont);
		    lon = dps_atof(l + 1);
		    DpsVarListReplaceDouble(&Doc->Sections, "geo.lat", lat);
		    DpsVarListReplaceDouble(&Doc->Sections, "geo.lon", lon);
		  }
		}else
		if(!strcasecmp(metaname, "ICBM")) {
		  double lat, lon;
		  char *l = strchr(metacont, (int)',');
		  if (l != NULL) {
		    lat = dps_atof(metacont);
		    lon = dps_atof(l + 1);
		    DpsVarListReplaceDouble(&Doc->Sections, "geo.lat", lat);
		    DpsVarListReplaceDouble(&Doc->Sections, "geo.lon", lon);
		  }
		} else if (strcasecmp(metaname, "title") && strcasecmp(metaname, "category") && strcasecmp(metaname, "tag")
			   && strcasecmp(metaname, "url")) {
		  if ((Sec = DpsVarListFind(&Doc->Sections, metaname)) != NULL) {
			DpsVarListReplaceStr(&Doc->Sections, metaname, metacont);
		  }
		}
	}
	else	if(!strcmp(name,"script"))	tag->script=opening;
	else	if(!strcmp(name,"style"))	tag->style=opening;
	else	if(!strcmp(name,"noindex"))	tag->comment=opening;
	else	
	if((!strcmp(name,"base"))&&(href)){
		
		DpsVarListReplaceStr(&Doc->Sections,"base.href",href);
		
		/* Do not add BASE HREF itself into database.      */
		/* It will be used only to compose relative links. */
		DPS_FREE(href);
	}

	if((href) && visible && (tag->follow != DPS_FOLLOW_NO) && !(Indexer->Flags.SkipHrefIn & DpsHrefFrom(name)) ) {
		DPS_HREF	Href;

		
		if ((Doc->subdoc < Indexer->Flags.SubDocLevel) && (Doc->sd_cnt < Indexer->Flags.SubDocCnt)
		    && (!strcasecmp(name, "IFRAME") || !strcasecmp(name, "EMBED") || !strcasecmp(name, "FRAME") 
					   || !strcasecmp(name, "META"))) {
		  DpsSGMLUnescape(href);
		  DpsIndexSubDoc(Indexer, Doc, base, NULL, href);
		} else {
/*		DpsSGMLUnescape(href); why we need do this ? */
		  DpsHrefInit(&Href);
		  Href.referrer = DpsVarListFindInt(&Doc->Sections, "Referrer-ID", 0);
		  Href.hops = 1 + DpsVarListFindInt(&Doc->Sections, "Hops", 0);
		  Href.site_id = 0; /*DpsVarListFindInt(&Doc->Sections, "Site_id", 0);*/
		  Href.url=href;
		  Href.method=DPS_METHOD_GET;
		  DpsHrefListAdd(Indexer, &Doc->Hrefs,&Href);
		  
		  /* For crosswords */
		  DPS_FREE(tag->lasthref);
		  tag->lasthref = (char*)DpsStrdup(href);
		}
	}
	DPS_FREE(metaname);
	DPS_FREE(metacont);
	DPS_FREE(href);
	DPS_FREE(base);
	DPS_FREE(lang);
	DPS_FREE(secname);
	
#ifdef WITH_PARANOIA
	DpsViolationExit(Indexer->handle, paran);
#endif
	return 0;
}


#define MAXSTACK	1024

typedef struct {
	size_t len;
	char * ofs;
} DPS_TAGSTACK;


int DpsHTMLParseBuf(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc, const char *section_name, const char *content) {
	DPS_HTMLTOK	tag;
	DPS_TEXTITEM	Item;
	const char	*htok;
	const char	*last;
	DPS_VAR		*BSec = DpsVarListFind(&Doc->Sections, (section_name) ? section_name : "body");
	DPS_VAR		*TSec = DpsVarListFind(&Doc->Sections, "title");
	int		body_sec  = BSec ? BSec->section : 0;
	int		title_sec = TSec ? TSec->section : 0;
	int		body_strict  = BSec ? BSec->strict : 0;
	int		title_strict = TSec ? TSec->strict : 0;

#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif
	
	bzero((void*)&Item, sizeof(Item));
	DpsHTMLTOKInit(&tag);
	tag.follow = Doc->Spider.follow;
	tag.index = Doc->Spider.index;
	tag.body = 1; /* for the case when the BodyPattern is applied */
	tag.comment = (strstr(content, "<!-- google_ad_section_start -->") == NULL) ? 0 : 2;

	htok=DpsHTMLToken(content, &last, &tag);
	
	while(htok){
	  char       *tmp=NULL;
	  const char *tmpbeg;
	  const char *tmpend;

	  switch(tag.type){
			
	  case DPS_HTML_COM:
	    break;

	  case DPS_HTML_TXT:

	    for( tmpbeg=htok;   tmpbeg<last && strchr(" \r\n\t",tmpbeg[0]) ; tmpbeg++);
	    for( tmpend=last-1; htok<tmpend && strchr(" \r\n\t",tmpend[0]) ; tmpend--);
	    if(tmpbeg>=tmpend)break;
				
	    tmp = DpsStrndup(tmpbeg,(size_t)(tmpend-tmpbeg+1));

	    if (BSec && !tag.comment && !tag.title && tag.body && !tag.script && !tag.style && tag.index && !tag.select 
		&& tag.visible[tag.level]) {
	      int z;
	      for(z = tag.level - 1; z >= 0 && tag.section[z] == 0; z--);
	      bzero((void*)&Item, sizeof(Item));
	      Item.href=tag.lasthref;
	      Item.str=tmp;
	      if (z >= 0) {
		Item.section = tag.section[z];
		Item.strict = tag.strict[z];
		Item.section_name = (section_name) ? section_name : tag.section_name[z];
	      } else {
		Item.section = body_sec;
		Item.strict = body_strict;
		Item.section_name = (section_name) ? section_name : "body";
	      }
	      Item.len = (size_t)(tmpend-tmpbeg+1);
	      DpsTextListAdd(&Doc->TextList,&Item);
	    }
	    if (TSec && (tag.comment != 1) && tag.title && tag.index && !tag.select && tag.visible[tag.level]) {
	      bzero((void*)&Item, sizeof(Item));
	      Item.href=NULL;
	      Item.str=tmp;
	      Item.section = title_sec;
	      Item.strict = title_strict;
	      Item.section_name = "title";
	      Item.len = (size_t)(tmpend-tmpbeg+1);
	      DpsTextListAdd(&Doc->TextList,&Item);
	    }
	    DPS_FREE(tmp);
	    break;
		
	  case DPS_HTML_TAG:
	    DpsHTMLParseTag(Indexer, &tag, Doc);
	    break;
	  }
	  htok = DpsHTMLToken(NULL, &last, &tag);
		
	}
	DPS_FREE(tag.lasthref);	
	DpsHTMLTOKFree(&tag);
#ifdef WITH_PARANOIA
	DpsViolationExit(Indexer->handle, paran);
#endif
	return DPS_OK;
}

int DpsHTMLParse(DPS_AGENT *Indexer, DPS_DOCUMENT *Doc) {
  DpsLog(Indexer, DPS_LOG_DEBUG, "Executing HTML parser");
  return DpsHTMLParseBuf(Indexer, Doc, NULL, (Doc->Buf.pattern == NULL) ? Doc->Buf.content : Doc->Buf.pattern);
}
