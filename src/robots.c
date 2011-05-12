/* Copyright (C) 2003-2011 DataPark Ltd. All rights reserved.
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
#include "dps_robots.h"
#include "dps_utils.h"
#include "dps_vars.h"
#include "dps_log.h"
#include "dps_doc.h"
#include "dps_server.h"
#include "dps_hash.h"
#include "dps_proto.h"
#include "dps_url.h"
#include "dps_mutex.h"
#include "dps_http.h"
#include "dps_indexer.h"
#include "dps_wild.h"
#include "dps_socket.h"
#include "dps_contentencoding.h"
#include "dps_sqldbms.h"
#include "dps_conf.h"
#include "dps_charsetutils.h"
#include "dps_db.h"
#include "dps_parsexml.h"
#include "dps_doc.h"
#include "dps_hrefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/types.h>

#define DPS_THREADINFO(A,s,m)	if(A->Conf->ThreadInfo)A->Conf->ThreadInfo(A,s,m)

static int DpsRobotCmp(DPS_ROBOT *r1, DPS_ROBOT *r2) {
  return strcasecmp(r1->hostinfo, r2->hostinfo);
}


DPS_ROBOT* DpsRobotFind(DPS_ROBOTS *Robots,const char *hostinfo){
	DPS_ROBOT *r, key;

	if (Robots->nrobots == 0) return NULL;
	if (Robots->nrobots == 1) return (strcasecmp(Robots->Robot->hostinfo, hostinfo) == 0) ? Robots->Robot : NULL;
	bzero(&key, sizeof(DPS_ROBOT));
	key.hostinfo = hostinfo;
	r = dps_bsearch(&key, Robots->Robot, Robots->nrobots, sizeof(DPS_ROBOT), (qsort_cmp)DpsRobotCmp);

	return r;
}

static DPS_ROBOT* DeleteRobotRules(DPS_AGENT *A, DPS_ROBOTS *Robots, const char *hostinfo) {
	DPS_ROBOT *robot;
	size_t i;
#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif
	
	if((robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(hostinfo))) != NULL) {
	  char buf[2*PATH_MAX];
	  DPS_DB *db;
	  dpshash32_t url_id = DpsStrHash32(DPS_NULL2EMPTY(hostinfo));
	  dps_snprintf(buf, sizeof(buf), "DELETE FROM robots WHERE hostinfo='%s'", DPS_NULL2EMPTY(hostinfo));
		      
	  if (A->flags & DPS_FLAG_UNOCON) {
	    db = &A->Conf->dbl.db[url_id % A->Conf->dbl.nitems];
#ifdef HAVE_SQL
	    DpsSQLAsyncQuery(db, NULL, buf);
#endif
	  } else {
	    db = &A->dbl.db[url_id % A->dbl.nitems];
#ifdef HAVE_SQL
	    DpsSQLAsyncQuery(db, NULL, buf);
#endif
	  }
		for(i=0;i<robot->nrules;i++){
			DPS_FREE(robot->Rule[i].path);
		}
		robot->nrules=0;
		DPS_FREE(robot->Rule);
#ifdef WITH_PARANOIA
		DpsViolationExit(A->handle, paran);
#endif
		return robot;
	}
#ifdef WITH_PARANOIA
	DpsViolationExit(A->handle, paran);
#endif
	return NULL;
}

static DPS_ROBOT* DpsRobotAddEmpty(DPS_ROBOTS *Robots, const char *hostinfo, time_t *last_crawled) {
  DPS_ROBOT *r;
#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif

	Robots->Robot = (DPS_ROBOT*)DpsRealloc(Robots->Robot, (Robots->nrobots + 1) * sizeof(DPS_ROBOT));
	if(Robots->Robot==NULL) {
	  Robots->nrobots = 0;
#ifdef WITH_PARANOIA
	  DpsViolationExit(-1, paran);
#endif
	  return NULL;
	}

	bzero((void*)&Robots->Robot[Robots->nrobots], sizeof(DPS_ROBOT));
	Robots->Robot[Robots->nrobots].hostinfo = (char*)DpsStrdup(DPS_NULL2EMPTY(hostinfo));
	if (last_crawled) {
	  Robots->Robot[Robots->nrobots].last_crawled = last_crawled;
	  Robots->Robot[Robots->nrobots].need_free = 0;
	} else {
	  Robots->Robot[Robots->nrobots].last_crawled = (time_t*)DpsMalloc(sizeof(time_t));
	  if (Robots->Robot[Robots->nrobots].last_crawled == NULL) {
#ifdef WITH_PARANOIA
	    DpsViolationExit(-1, paran);
#endif
	    return NULL;
	  }
	  *(Robots->Robot[Robots->nrobots].last_crawled) = (time_t)0;
	  Robots->Robot[Robots->nrobots].need_free = 1;
	}
	
	Robots->nrobots++;
	if (Robots->nrobots > 1) {
	  DpsSort(Robots->Robot, Robots->nrobots, sizeof(DPS_ROBOT), (qsort_cmp)DpsRobotCmp);
	  r = DpsRobotFind(Robots, hostinfo);
	} else {
	  r = &Robots->Robot[Robots->nrobots - 1];
	}
#ifdef WITH_PARANOIA
	DpsViolationExit(-1, paran);
#endif
	return r;
}

static int AddRobotRule(DPS_AGENT *A, DPS_ROBOT *robot, int cmd, const char *path, int insert_flag) {
#ifdef HAVE_SQL
  DPS_DB *db;
  dpshash32_t url_id;
#ifdef WITH_PARANOIA
        void *paran = DpsViolationEnter(paran);
#endif

	if (cmd == DPS_METHOD_CRAWLDELAY) {
	  robot->crawl_delay = (size_t)(1000 * DPS_ATOF(path));
	} 
	{

	  robot->Rule = (DPS_ROBOT_RULE*)DpsRealloc(robot->Rule, (robot->nrules + 1) * sizeof(DPS_ROBOT_RULE));
	  if(robot->Rule==NULL) {
	    robot->nrules = 0;
#ifdef WITH_PARANOIA
	    DpsViolationExit(A->handle, paran);
#endif
	    return DPS_ERROR;
	  }
	
	  robot->Rule[robot->nrules].cmd = cmd;
	  robot->Rule[robot->nrules].path = (char*)DpsStrdup(DPS_NULL2EMPTY(path));
	  robot->Rule[robot->nrules].len = dps_strlen(robot->Rule[robot->nrules].path);
	  robot->nrules++;
	}

	if (insert_flag) {
	  char buf[2*PATH_MAX+128];
	  char path_esc[2*PATH_MAX+1];
	  url_id = DpsStrHash32(robot->hostinfo);

	  if (A->flags & DPS_FLAG_UNOCON) {
	    db = &A->Conf->dbl.db[url_id % A->Conf->dbl.nitems];
	  } else {
	    db = &A->dbl.db[url_id % A->dbl.nitems];
	  }

	  (void)DpsDBEscStr(db, path_esc, DPS_NULL2EMPTY(path), dps_min(PATH_MAX,dps_strlen(DPS_NULL2EMPTY(path))));
	  dps_snprintf(buf, sizeof(buf), "INSERT INTO robots(cmd,ordre,added_time,hostinfo,path)VALUES(%d,%d,%d,'%s','%s')",
		       cmd, robot->nrules, A->now, robot->hostinfo, path_esc);
	  DpsSQLAsyncQuery(db, NULL, buf);

	}
#ifdef WITH_PARANOIA
	DpsViolationExit(A->handle, paran);
#endif

#endif /*HAVE_SQL*/
	return DPS_OK;
}

int DpsRobotListFree(DPS_ROBOTS *Robots){
	size_t i,j; 
#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif
	
	if(!Robots->nrobots){
#ifdef WITH_PARANOIA
	  DpsViolationExit(-1, paran);
#endif
	  return 0;
	}
	for(i=0;i<Robots->nrobots;i++){
		for(j=0;j<Robots->Robot[i].nrules;j++){
			DPS_FREE(Robots->Robot[i].Rule[j].path);
		}
		DPS_FREE(Robots->Robot[i].hostinfo);
		DPS_FREE(Robots->Robot[i].Rule);
		if (Robots->Robot[i].need_free) DPS_FREE(Robots->Robot[i].last_crawled);
	}
	DPS_FREE(Robots->Robot);
	Robots->nrobots=0;
#ifdef WITH_PARANOIA
	DpsViolationExit(-1, paran);
#endif
	return 0;
}

static DPS_ROBOT_RULE DpsRobotErrRule = {DPS_METHOD_VISITLATER, "", 0};

static DPS_ROBOT *DpsRobotClone(DPS_AGENT *Indexer, DPS_ROBOTS *Robots, DPS_SERVER *Server, 
				DPS_DOCUMENT *Doc, DPS_URL *URL, char *rurl, size_t rurlen) {
	DPS_ROBOT *robot, *rI = NULL;
#ifdef HAVE_SQL
	DPS_SERVER	*rServer;
	DPS_DOCUMENT	*rDoc;
       	int           status, result;
#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif
	TRACE_IN(Indexer, "DpsRobotClone");

	DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
	if (Robots->nrobots == 3 * DPS_SERVERID_CACHE_SIZE) {
	  robot = NULL;
	  Robots->serial++;
	  DpsRobotListFree(Robots);
	} else {
	  robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo));
	}

	if (robot == NULL) {
	  char buf[2*PATH_MAX];
	  dpshash32_t url_id = DpsStrHash32(URL->hostinfo);
	  DPS_DB *db;
	  DPS_SQLRES Res;
	  size_t i, rows;
	  int rc, cmd;

	  DpsSQLResInit(&Res);
	  dps_snprintf(buf, sizeof(buf), "SELECT cmd,path FROM robots WHERE hostinfo='%s' ORDER BY ordre", URL->hostinfo);
	  if (Indexer->flags & DPS_FLAG_UNOCON) {
	    db = &Indexer->Conf->dbl.db[url_id % Indexer->Conf->dbl.nitems];
	  } else {
	    db = &Indexer->dbl.db[url_id % Indexer->dbl.nitems];
	  }
	  if(DPS_OK == (rc = DpsSQLQuery(db, &Res, buf))) {
	    rows = DpsSQLNumRows(&Res);
	    if (rows > 0) {
	      DpsRobotAddEmpty(&Indexer->Conf->Robots, DPS_NULL2EMPTY(URL->hostinfo), NULL);
	      robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo));
	      for(i = 0; i < rows; i++) {
		cmd = atoi(DpsSQLValue(&Res,i,0));
		if (cmd != DPS_METHOD_UNKNOWN)
		  AddRobotRule(Indexer, robot, cmd, DpsSQLValue(&Res,i,1), 0);
	      }
	    }
	  }
	  DpsSQLFree(&Res);
	}

	if (robot == NULL) {
  
	  DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);

	  rDoc = DpsDocInit(NULL);
	  DpsSpiderParamInit(&rDoc->Spider);
	  rDoc->Buf.max_size = (size_t)DpsVarListFindInt(&Indexer->Vars, "MaxDocSize", DPS_MAXDOCSIZE);
	  rDoc->Buf.allocated_size = DPS_NET_BUF_SIZE;
	  if ((rDoc->Buf.buf = (char*)DpsMalloc(rDoc->Buf.allocated_size + 1)) == NULL) {
	    DpsDocFree(rDoc);
	    TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
	    DpsViolationExit(Indexer->handle, paran);
#endif
	    return NULL;
	  }
	  rDoc->Buf.buf[0]='\0';
	  rDoc->subdoc = Indexer->Flags.SubDocLevel + 1;

	  dps_snprintf(rurl, rurlen, "%s://%s/robots.txt", DPS_NULL2EMPTY(URL->schema), DPS_NULL2EMPTY(URL->hostinfo));
	  DpsVarListAddStr(&rDoc->Sections, "URL", rurl);
	  DpsURLParse(&rDoc->CurURL, rurl);
	  DpsLog(Indexer, DPS_LOG_INFO, "ROBOTS: %s", rurl);

	  if (Server != NULL) rServer = Server;
	  else rServer = DpsServerFind(Indexer, (urlid_t)DpsVarListFindInt(&Doc->Sections, "Server_id", 0), rurl, URL->charset_id, NULL);

	  if (Doc != NULL) {
	    DpsVarListReplaceLst(&rDoc->RequestHeaders, &Doc->RequestHeaders, NULL, "*"); 
	  } else {
	    DpsDocAddDocExtraHeaders(Indexer, rDoc);
	    DpsDocAddConfExtraHeaders(Indexer->Conf, rDoc);
	  }

	  if (rServer != NULL) {
	    DpsVarListReplaceLst(&rDoc->Sections, &rServer->Vars, NULL, "*");
	    DpsDocAddServExtraHeaders(rServer, rDoc);
	    DpsVarList2Doc(rDoc, rServer);
	  } else {
	    DpsSpiderParamInit(&rDoc->Spider);
	  }
	  DpsVarListLog(Indexer, &rDoc->RequestHeaders, DPS_LOG_DEBUG, "ROBOTS.Request");

	  if (Doc == NULL) {
	    DpsDocLookupConn(Indexer, rDoc);
	  } else {
	    DPS_FREE(rDoc->connp.connp);
	    rDoc->connp = Doc->connp;
	  }
	  result = DpsGetURL(Indexer, rDoc, NULL);
/*	  DpsParseHTTPResponse(Indexer, rDoc);*/
	  DpsDocProcessResponseHeaders(Indexer, rDoc);
	  DpsVarListLog(Indexer, &rDoc->Sections, DPS_LOG_DEBUG, "ROBOTS.Response");

	  DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
	  robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo));
	  
	  if (robot == NULL) {
	    if ((status = DpsVarListFindInt(&rDoc->Sections, "Status", 0)) == DPS_HTTP_STATUS_OK) {
	      const char	*ce = DpsVarListFindStr(&rDoc->Sections, "Content-Encoding", "");
#ifdef HAVE_ZLIB
	      if(!strcasecmp(ce, "gzip") || !strcasecmp(ce, "x-gzip")){
		DPS_THREADINFO(Indexer,"UnGzip", rurl);
		DpsUnGzip(Indexer, rDoc);
		DpsVarListReplaceInt(&rDoc->Sections, "Content-Length", rDoc->Buf.buf - rDoc->Buf.content + (int)rDoc->Buf.size);
	      } else if(!strcasecmp(ce, "deflate")) {
		DPS_THREADINFO(Indexer,"Inflate",rurl);
		DpsInflate(Indexer, rDoc);
		DpsVarListReplaceInt(&rDoc->Sections, "Content-Length", rDoc->Buf.buf - rDoc->Buf.content + (int)rDoc->Buf.size);
	      }else if(!strcasecmp(ce, "compress") || !strcasecmp(ce, "x-compress")) {
		DPS_THREADINFO(Indexer,"Uncompress",rurl);
		DpsUncompress(Indexer, rDoc);
		DpsVarListReplaceInt(&rDoc->Sections, "Content-Length", rDoc->Buf.buf - rDoc->Buf.content + (int)rDoc->Buf.size);
	      }else
#endif
	      if(!strcasecmp(ce, "identity") || !strcasecmp(ce, "")) {
		/* Nothing to do*/
	      }else{
		DpsLog(Indexer,DPS_LOG_ERROR,"Unsupported Content-Encoding");
/*	          DpsVarListReplaceInt(&rDoc->Sections, "Status", status = DPS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);*/
	      }
	      if (status == DPS_HTTP_STATUS_OK) 
		result = DpsRobotParse(Indexer, rServer, rDoc->Buf.content, (char*)DPS_NULL2EMPTY(rDoc->CurURL.hostinfo));
	      else {
		DpsRobotAddEmpty(&Indexer->Conf->Robots, DPS_NULL2EMPTY(rDoc->CurURL.hostinfo), NULL);
		if ((robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo))) != NULL) {
		  if(AddRobotRule(Indexer, robot, DPS_METHOD_UNKNOWN, "/", 1)) {
		    DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
		  }
		}
	      }
/*	    } else if ((status == 0) || (status >= 500)) {
	      Doc.method = DPS_METHOD_VISITLATER;*/
	    } else {
	      DpsRobotAddEmpty(&Indexer->Conf->Robots, DPS_NULL2EMPTY(URL->hostinfo), NULL);
	      if ((robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo))) != NULL) {
		if(AddRobotRule(Indexer, robot, DPS_METHOD_UNKNOWN, "/", 1)) {
		  DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
		}
	      }
	    }
	    robot = DpsRobotFind(Robots, DPS_NULL2EMPTY(URL->hostinfo));
	  }
	  if (Doc != NULL) bzero(&rDoc->connp, sizeof(rDoc->connp));
	  DpsDocFree(rDoc);
	  
	}

	if (robot != NULL) {
	  rI = DeleteRobotRules(Indexer, &Indexer->Robots, DPS_NULL2EMPTY(URL->hostinfo));
	  if (rI == NULL) rI = DpsRobotAddEmpty(&Indexer->Robots, DPS_NULL2EMPTY(URL->hostinfo), robot->last_crawled);
	  if (rI != NULL) {
	    register size_t j;
	    rI->crawl_delay = robot->crawl_delay;
	    for(j = 0; j < robot->nrules; j++) {
	      if (robot->Rule[j].cmd != DPS_METHOD_UNKNOWN)
		AddRobotRule(Indexer, rI, robot->Rule[j].cmd, robot->Rule[j].path, 0);
	    }
	  }
	}

	DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
	TRACE_OUT(Indexer);
#ifdef WITH_PARANOIA
	DpsViolationExit(Indexer->handle, paran);
#endif

#endif /*HAVE_SQL*/
	return rI;
}


static DPS_ROBOT_RULE dps_host_disallow = {DPS_METHOD_VISITLATER, "No Host: directive found", 24};
static DPS_ROBOT_RULE dps_host_crawldelay = {DPS_METHOD_CRAWLDELAY, "Too big Crawl-delay: postponing the doc", 39};

DPS_ROBOT_RULE* DpsRobotRuleFind(DPS_AGENT *Indexer, DPS_SERVER *Server, DPS_DOCUMENT *Doc, DPS_URL *pURL, int make_pause, int aliased) {
        char l_rurl[PATH_MAX];
	DPS_DOCUMENT *HostDoc;
        DPS_ROBOT_RULE *r;
	DPS_ROBOT *robot;
	DPS_URL *URL;
	DPS_ROBOTS *Robots = &Indexer->Conf->Robots; 
	const char *hostname;
	char		*rurl = l_rurl;
	size_t        rurlen, j;
	int have_host = 0, u;
#ifdef WITH_PARANOIA
	void *paran = DpsViolationEnter(paran);
#endif

	URL = (Doc == NULL) ? pURL : &Doc->CurURL;

	if (strcasecmp(DPS_NULL2EMPTY(URL->schema), "http")) { /* robots.txt exist only for http scheme */
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return NULL;
	}
	
	HostDoc = DpsDocInit(NULL);

	rurlen = URL->len + 32;
/*	rurlen = 32 + dps_strlen(DPS_NULL2EMPTY(URL->schema)) + dps_strlen(DPS_NULL2EMPTY(URL->hostinfo)) +
	  dps_strlen(DPS_NULL2EMPTY(URL->specific)) + dps_strlen(DPS_NULL2EMPTY(URL->path)) + dps_strlen(DPS_NULL2EMPTY(URL->query_string))
	  + dps_strlen(DPS_NULL2EMPTY(URL->filename));*/
	if (rurlen > PATH_MAX) {
	  rurl = (char*)DpsMalloc(rurlen);
	}
	if ( rurl == NULL) {
	  DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return &DpsRobotErrRule;
	}

	hostname = DPS_NULL2EMPTY(URL->hostinfo);

	DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
	u = ((Indexer->Robots.nrobots == DPS_SERVERID_CACHE_SIZE) || (Indexer->Robots.serial != Robots->serial));
	DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
	if (u) {
	  robot = NULL;
	  DpsRobotListFree(&Indexer->Robots);
	  Indexer->Robots.serial = Robots->serial;
	} else {
	  robot = DpsRobotFind(&Indexer->Robots, hostname);
	}
	if (robot == NULL) {
	  if (Indexer->flags & DPS_FLAG_UNOCON) DPS_GETLOCK(Indexer, DPS_LOCK_DB);
	  robot = DpsRobotClone(Indexer, Robots, Server, Doc, URL, rurl, rurlen);
	  if (Indexer->flags & DPS_FLAG_UNOCON) DPS_RELEASELOCK(Indexer, DPS_LOCK_DB);
	}

	if (robot != NULL) {

	  if (make_pause) {
	    if (Server->crawl_delay > robot->crawl_delay) {
	      size_t to_sleep, diff;
	      time_t now;

	      now = time(NULL);
	      DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
	      diff = (size_t) (now - *(Server->last_crawled));
	      while ((time_t)(1000 * diff) < Server->crawl_delay) {
		to_sleep = Server->crawl_delay - diff * 1000;
		if ( (to_sleep > Indexer->Flags.MaxCrawlDelay * 1000) || (Indexer->action == DPS_TERMINATED) ) {
		  time_t		next_index_time;
		  char dbuf[64];
		  DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
		  next_index_time = Indexer->now + diff;
		  dps_snprintf(dbuf, sizeof(dbuf), "%lu", (next_index_time & 0x80000000) ? 0x7fffffff : next_index_time);
		  DpsVarListReplaceStr(&Doc->Sections,"Next-Index-Time",dbuf);
		  DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
		  DpsViolationExit(Indexer->handle, paran);
#endif
		  return &dps_host_crawldelay;
		}
		DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
		DpsLog(Indexer, DPS_LOG_EXTRA, "Server.%s.Crawl-delay: %d of %d msec.", 
		       Server->Match.pattern, to_sleep, Server->crawl_delay);
		DPS_MSLEEP(to_sleep);
		DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
		now = time(NULL);
		diff = (size_t) (now - *(Server->last_crawled));
	      }
	      *(Server->last_crawled) = Indexer->now = now;
	      DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);

	    }else if (robot->crawl_delay > 0 && Doc != NULL) {
	      size_t to_sleep, diff;
	      time_t now;

	      DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);

	      if (Indexer->Robots.serial != Robots->serial) {
		DpsRobotListFree(&Indexer->Robots);
		Indexer->Robots.serial = Robots->serial;
		if (Indexer->flags & DPS_FLAG_UNOCON) DPS_GETLOCK(Indexer, DPS_LOCK_DB);
		robot = DpsRobotClone(Indexer, Robots, Server, Doc, URL, rurl, rurlen);
		if (Indexer->flags & DPS_FLAG_UNOCON) DPS_RELEASELOCK(Indexer, DPS_LOCK_DB);
	      }

	      now = time(NULL);
	      diff = (size_t) (now - *(robot->last_crawled));
	      while ((time_t)(1000 * diff) < robot->crawl_delay) {
		to_sleep = robot->crawl_delay - 1000 * diff;
		if ( (to_sleep > Indexer->Flags.MaxCrawlDelay * 1000) || (Indexer->action == DPS_TERMINATED) ) {
		  time_t		next_index_time;
		  char dbuf[64];
		  DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
		  next_index_time = Indexer->now + diff;
		  dps_snprintf(dbuf, sizeof(dbuf), "%lu", (next_index_time & 0x80000000) ? 0x7fffffff : next_index_time);
		  DpsVarListReplaceStr(&Doc->Sections,"Next-Index-Time",dbuf);
		  DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
		  DpsViolationExit(Indexer->handle, paran);
#endif
		  return &dps_host_crawldelay;
		}
		DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
		DpsLog(Indexer, DPS_LOG_EXTRA, "%s/robots.txt: Crawl-delay: %d of %d msec.", 
		       robot->hostinfo, to_sleep, robot->crawl_delay);
		DPS_MSLEEP(to_sleep);
		DPS_GETLOCK(Indexer, DPS_LOCK_ROBOTS);
		now = time(NULL);
		diff = (size_t) (now - *(robot->last_crawled));
	      }
	      *(robot->last_crawled) = Indexer->now = now;
	      DPS_RELEASELOCK(Indexer, DPS_LOCK_ROBOTS);
	    }
	  }

	 dps_snprintf(rurl, rurlen, "%s%s%s", DPS_NULL2EMPTY(URL->path), DPS_NULL2EMPTY(URL->filename), DPS_NULL2EMPTY(URL->query_string));
		for(j=0;j<robot->nrules;j++){
			/* FIXME: compare full URL */
			if(!strncmp(rurl /*DPS_NULL2EMPTY(URL->path)*/, robot->Rule[j].path, robot->Rule[j].len)) {
			  DpsLog(Indexer, DPS_LOG_DEBUG, "ROBOTS path: %s, pathlen:%d URL: %s  cmd: %s", 
				 robot->Rule[j].path, robot->Rule[j].len, rurl, DpsMethodStr(robot->Rule[j].cmd));
			  r = &robot->Rule[j];
			  if (rurlen > PATH_MAX) DPS_FREE(rurl);
			  DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
			  DpsViolationExit(Indexer->handle, paran);
#endif
			  return r;

			} else if (!DpsWildCmp(rurl, robot->Rule[j].path)) {
			  DpsLog(Indexer, DPS_LOG_DEBUG, "ROBOTS wild: %s, URL: %s  cmd: %s", 
				 robot->Rule[j].path, rurl, DpsMethodStr(robot->Rule[j].cmd));
			  r = &robot->Rule[j];
			  if (rurlen > PATH_MAX) DPS_FREE(rurl);
			  DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
			  DpsViolationExit(Indexer->handle, paran);
#endif
			  return r;

			} else if (robot->Rule[j].cmd == DPS_METHOD_HOST) {
			  char robohost[512];
			  dps_snprintf(robohost, sizeof(robohost), "http://%s/", robot->Rule[j].path);
			  DpsVarListReplaceStr(&HostDoc->Sections, "URL", robohost);
			  DpsVarListDel(&HostDoc->Sections, "E_URL");
			  DpsVarListReplaceStr(&HostDoc->Sections, "DP_ID", "0");
			  if (DPS_OK == DpsURLAction(Indexer, HostDoc, DPS_URL_ACTION_FINDBYURL)) {
			    urlid_t host_url_id = DpsVarListFindInt(&HostDoc->Sections, "DP_ID", 0);
			    if (host_url_id != 0) have_host = 1;
			  }
/*			  have_host = 1;*/
			  if (!strncmp(DPS_NULL2EMPTY(URL->hostinfo), robot->Rule[j].path, robot->Rule[j].len)) {
			    DpsLog(Indexer, DPS_LOG_DEBUG, "ROBOTS host: %s allowed", robot->Rule[j].path);
			    if (rurlen > PATH_MAX) DPS_FREE(rurl);
			    DpsDocFree(HostDoc);
#ifdef WITH_PARANOIA
			    DpsViolationExit(Indexer->handle, paran);
#endif
			    return NULL;
			  }
			}
		}
	}
	if (rurlen > PATH_MAX) DPS_FREE(rurl);
	DpsDocFree(HostDoc);
	if (have_host && !aliased) {
#ifdef WITH_PARANOIA
	  DpsViolationExit(Indexer->handle, paran);
#endif
	  return &dps_host_disallow;
	}
#ifdef WITH_PARANOIA
	DpsViolationExit(Indexer->handle, paran);
#endif
	return NULL;
}


void DpsRobotClean(DPS_AGENT *A) {
	char buf[256];
	DPS_DB	*db;
	size_t i, dbfrom = 0, dbto;
	int res;

	if (A->Flags.robots_period == 0) return;

	dps_snprintf(buf, sizeof(buf), "DELETE FROM robots WHERE added_time < %d", A->now - A->Flags.robots_period);

	if (A->flags & DPS_FLAG_UNOCON) {
	  DPS_GETLOCK(A, DPS_LOCK_CONF);
	  dbto =  A->Conf->dbl.nitems;
	  DPS_RELEASELOCK(A, DPS_LOCK_CONF);
	} else dbto = A->dbl.nitems;

	for (i = dbfrom; i < dbto; i++) {
	  db = (A->flags & DPS_FLAG_UNOCON) ? &A->Conf->dbl.db[i] : &A->dbl.db[i];
	  if (A->flags & DPS_FLAG_UNOCON) DPS_GETLOCK(A, DPS_LOCK_DB);
#ifdef HAVE_SQL
	  res = DpsSQLAsyncQuery(db, NULL, buf);
#endif
	  if(res != DPS_OK){
		DpsLog(A, DPS_LOG_ERROR, db->errstr);
	  }
	  if (A->flags & DPS_FLAG_UNOCON) DPS_RELEASELOCK(A, DPS_LOCK_DB);
	  if (res != DPS_OK) break;
	}
}


static int Text (DPS_XML_PARSER *parser, const char *s, size_t len) {
  XML_PARSER_DATA *D = parser->user_data;
  /*  DPS_AGENT *Indexer = D->Indexer;*/
  DPS_DOCUMENT *Doc = D->Doc;
  char *value = DpsStrndup(s, len);

  if (D->sec != NULL) {
    if (!strcasecmp(D->sec, "loc")) {
      DpsVarListReplaceStr(&Doc->Sections, "URL", value);
    } else if (!strcasecmp(D->sec, "lastmod")) {
      /* FIXME: implemet the check if the URL has been changed */
    } else if (!strcasecmp(D->sec, "changefreq")) {
      /* FIXME: implement the adjusting of next_index_time according this value */
    } else if (!strcasecmp(D->sec, "priority")) {
      DpsVarListReplaceStr(&Doc->Sections, "Pop_Rank", value);
    }
  }

  DPS_FREE(value);
  return DPS_XML_OK;
}


static int DpsSitemapEndElement(DPS_XML_PARSER *parser, const char *name, size_t l) {
  XML_PARSER_DATA *D = parser->user_data;
  size_t i = l;
  char *p;

  if (!strcasecmp(D->sec, "loc")) {
    DPS_AGENT *Indexer = D->Indexer;
    DPS_DOCUMENT *Doc = D->Doc;
    DPS_HREF Href;

    DpsHrefInit(&Href);
    Href.url = DpsVarListFindStr(&Doc->Sections, "URL", NULL);
    if (Href.url) {
      Href.method = DPS_METHOD_GET;
      Href.checked = 0;
      Href.weight = (float)DPS_ATOF(DpsVarListFindStr(&Doc->Sections, "Pop_Rank", "0.5"));
      DpsHrefListAdd(Indexer, &Indexer->Hrefs, &Href);
      if(Indexer->Hrefs.nhrefs > 1024) DpsStoreHrefs(Indexer);
    }
    DpsVarListFree(&Doc->Sections);
  }

  while (i && name[i] != '.') i--;

  DPS_FREE(D->secpath);
  D->secpath = DpsStrndup(name, i);
  DPS_FREE(D->sec);
  p = strrchr(D->secpath, '.');
  D->sec = p ? DpsStrdup(p + 1) : DpsStrndup(name, i);
  return(DPS_XML_OK);
}



static int DpsSitemapParse(DPS_AGENT *Indexer, const char *content) {
  XML_PARSER_DATA Data;
  DPS_XML_PARSER parser;
  DPS_DOCUMENT Doc;
  int res = DPS_OK;

  DpsLog(Indexer, DPS_LOG_INFO, "Executing Sitemap parser");

  DpsDocInit(&Doc);

  DpsXMLParserCreate(&parser);
  bzero(&Data, sizeof(Data));
  Data.Indexer = Indexer;
  Data.Doc = &Doc;

  DpsXMLSetUserData(&parser, &Data);
  DpsXMLSetEnterHandler(&parser, DpsXMLstartElement);
  DpsXMLSetLeaveHandler(&parser, DpsSitemapEndElement);
  DpsXMLSetValueHandler(&parser, Text);

  if (DpsXMLParser(&parser, 0, content, (int)dps_strlen(content)) == DPS_XML_ERROR) {
    char err[256];    
    dps_snprintf(err, sizeof(err), 
                 "Sitemap parsing error: %s at line %d pos %d\n",
                  DpsXMLErrorString(&parser),
                  DpsXMLErrorLineno(&parser),
                  DpsXMLErrorPos(&parser));
    DpsLog(Indexer, DPS_LOG_ERROR, err);
    res = DPS_ERROR;
  }

  DpsXMLParserFree(&parser);
  DPS_FREE(Data.sec);
  DPS_FREE(Data.secpath);
  DpsDocFree(&Doc);
  DpsStoreHrefs(Indexer);

  return res;
}


static char *dps_robots_normalise(const char *s) { /* robots.txt path normalisation, blame to Google */
  char *norm = (char*)DpsMalloc(2 * dps_strlen(s) + 1);
  char *d = norm;
  int prev_asterisk = 0, prev_dollar = 0, prev_slash = 0;
  if (norm != NULL) {
    do {
      switch(*s) {
      case '\0':
      case '\r':
      case '\n':
	break;
      case '$':
	prev_dollar = 1;
	prev_slash = 0;
	prev_asterisk = 0;
	*d++ = *s++;
	break;
      case '?':
	if (prev_asterisk) *d++ = '\\';
	*d++ = *s++;
	prev_asterisk = 0;
	prev_slash = 0;
	prev_dollar = 0;
	break;
      case '\\':
	if (!prev_slash) prev_slash = 1; else prev_slash = 0;
	prev_asterisk = 0; 
	prev_dollar = 0;
	*d++ = *s++;
	break;
      case '*': 
	if (!prev_asterisk) *d++ = *s++;
        prev_asterisk = !prev_slash;
	prev_slash = 0;
	prev_dollar = 0;
	break;
      default: 
	prev_asterisk = 0; 
	prev_slash = 0;
	prev_dollar = 0;
	*d++ = *s++;
	break;
      }
    } while(*s != '\0' && *s != '\n' && *s != '\r');
    if (prev_dollar) d--;
    *d = '\0';
  }
  return norm;
}


int DpsRobotParse(DPS_AGENT *Indexer, DPS_SERVER *Srv, const char *content, const char *hostinfo) {
        DPS_ENV *Conf = Indexer->Conf;
        DPS_ROBOTS *Robots = &Conf->Robots;
	DPS_ROBOT *robot;
	int rule = 0, common = 0, my = 0, newrecord = 1, has_cmd = 0;
	char *s,*e,*lt;
	char *agent = NULL;
	const char *UA = (Srv != NULL) ? DpsVarListFindStr(&Srv->Vars, "Request.User-Agent", DPS_USER_AGENT) :
	  DpsVarListFindStr(&Indexer->Vars, "Request.User-Agent", DPS_USER_AGENT);

	/* Wipe out any existing (default) rules for this host */
	robot=DeleteRobotRules(Indexer, Robots, DPS_NULL2EMPTY(hostinfo));
	if (robot == NULL) robot = DpsRobotAddEmpty(Robots, DPS_NULL2EMPTY(hostinfo), NULL);
	if(robot==NULL) return(DPS_ERROR);
	
	if(content==NULL) return(DPS_OK);
/*
	fprintf(stderr, "ROBOTS CONTENT: %s\n", content);
*/
	s = (char*)content;
	while (*s && (*s == NL_CHAR || *s == CR_CHAR)) s++;
	lt = s;
	while(*lt && (*lt != NL_CHAR) && (*lt != CR_CHAR)) lt++;
	if (*lt == CR_CHAR) *lt++ = '\0';
	if (*lt) *lt++ = '\0';


	while(*s || *lt){
/*
	  fprintf(stderr, " s:%s|\nlt:%s|\n", s, lt);
	  fprintf(stderr, "my:%d rule:%d common:%d newrecord:%d\n", my, rule, common, newrecord);
*/
	        if (*s == '\0') {
		  newrecord = 1; has_cmd = 0;
		  rule = 0;
		}
		if(*s=='#'){
		}else
		if(!(strncasecmp(s,"User-Agent:", 11))){
			
			agent = DpsTrim(s+11," \t\r\n");
			if (has_cmd) {
			  newrecord = 1; has_cmd = 0;
			}
			if (newrecord) {
			  newrecord = 0;
			  rule = 0;
			}

			/* The "*" User-Agent is important only */
			/* if no other rules apply */
			if(!strcmp(agent, "*")) {
			  if (my) { 
			        rule = 0; 
			  } else {
				rule = 1;
				common = 1;
			  }
			} else if(!strncasecmp(agent, UA, dps_strlen(agent)) || (strcmp(agent,"*") && !DpsWildCaseCmp(UA, agent))) {
			        rule = 1; my = 1;
				if (common) {
				  robot = DeleteRobotRules(Indexer, Robots, DPS_NULL2EMPTY(hostinfo));
				  common = 0;
				}
			}
		}else { has_cmd = 1;
		  if((!(strncasecmp(s, "Disallow", 8))) && (rule)) {

			if((e=strchr(s+9,'#')))*e=0;
			e=s+9;DPS_SKIP(e," \t");s=e;
			DPS_SKIPN(e," \t");*e=0;
			if(s && *s) {
			  char *norm = dps_robots_normalise(s);
			  if((norm != NULL) && AddRobotRule(Indexer, robot, DPS_METHOD_VISITLATER, norm, 1)) {
			    DPS_FREE(norm);
			    DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
			    return DPS_ERROR;
			  }
			  DPS_FREE(norm);
			} else { /* Empty Disallow == Allow all */
			  if(AddRobotRule(Indexer, robot, DPS_METHOD_GET, "/", 1)) {
				  DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
					return(DPS_ERROR);
			  }
			}
		  }else
		  if((!(strncasecmp(s, "Allow", 5))) && (rule)) {
			if((e=strchr(s+6,'#')))*e=0;
			e=s+6;DPS_SKIP(e," \t");s=e;
			DPS_SKIPN(e," \t");*e=0;
			if(s && *s){
			  char *norm = dps_robots_normalise(s);
			  if((norm != NULL) && AddRobotRule(Indexer, robot,DPS_METHOD_GET, norm, 1)) {
			    DPS_FREE(norm);
			    DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
			    return DPS_ERROR;
			  }
			  DPS_FREE(norm);
			}
		  }else
		  if((!(strncasecmp(s, "Host", 4))) && (rule)) {
			if((e=strchr(s+5,'#')))*e=0;
			e=s+5;DPS_SKIP(e," \t");s=e;
			DPS_SKIPN(e," \t");*e=0;
			if(s && *s){
			  if(AddRobotRule(Indexer, robot, DPS_METHOD_HOST, s, 1)) {
				  DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
					return(DPS_ERROR);
			  }
			}
		  }else
		  if((!(strncasecmp(s, "Sitemap", 7))) && (rule)) {
			if((e=strchr(s+8,'#')))*e=0;
			e=s+8;DPS_SKIP(e," \t");s=e;
			DPS_SKIPN(e," \t");*e=0;
			if(s && *s) {
			  DPS_SERVER	*mServer;
			  DPS_DOCUMENT	*mDoc;
			  int status, result;

			  mDoc = DpsDocInit(NULL);
			  DpsSpiderParamInit(&mDoc->Spider);
			  mDoc->Buf.max_size = (size_t)DpsVarListFindInt(&Indexer->Vars, "MaxDocSize", DPS_MAXDOCSIZE);
			  mDoc->Buf.allocated_size = DPS_NET_BUF_SIZE;
			  if ((mDoc->Buf.buf = (char*)DpsMalloc(mDoc->Buf.allocated_size + 1)) == NULL) {
			    DpsDocFree(mDoc);
			    return DPS_ERROR;
			  }
			  mDoc->Buf.buf[0]='\0';
			  mDoc->subdoc = Indexer->Flags.SubDocLevel + 1;

			  DpsVarListAddStr(&mDoc->Sections, "URL", s);
			  DpsURLParse(&mDoc->CurURL, s);
			  DpsLog(Indexer, DPS_LOG_INFO, "Sitemap: %s", s);

			  if (Srv != NULL) mServer = Srv;
			  else mServer = DpsServerFind(Indexer, 0, s, mDoc->CurURL.charset_id, NULL);

			  DpsDocAddDocExtraHeaders(Indexer, mDoc);
			  DpsDocAddConfExtraHeaders(Indexer->Conf, mDoc);

			  if (mServer != NULL) {
			    DpsVarListReplaceLst(&mDoc->Sections, &mServer->Vars, NULL, "*");
			    DpsDocAddServExtraHeaders(mServer, mDoc);
			    DpsVarList2Doc(mDoc, mServer);
			  } else {
			    DpsSpiderParamInit(&mDoc->Spider);
			  }
			  DpsVarListLog(Indexer, &mDoc->RequestHeaders, DPS_LOG_DEBUG, "Sitemap.Request");

			  DpsDocLookupConn(Indexer, mDoc);

			  result = DpsGetURL(Indexer, mDoc, NULL);
			  /*	  DpsParseHTTPResponse(Indexer, mDoc);*/
			  DpsDocProcessResponseHeaders(Indexer, mDoc);
			  DpsVarListLog(Indexer, &mDoc->Sections, DPS_LOG_DEBUG, "Sitemap.Response");

			  if ((status = DpsVarListFindInt(&mDoc->Sections, "Status", 0)) == DPS_HTTP_STATUS_OK) {
			    const char	*ce = DpsVarListFindStr(&mDoc->Sections, "Content-Encoding", "");
			    const char	*ct = DpsStrdup(DpsVarListFindStr(&mDoc->Sections, "Content-Type", ""));
			    char *p = strchr(ct, (int)';');

			    if (p != NULL) *p = '\0';
#ifdef HAVE_ZLIB
			    if(!strcasecmp(ce, "gzip") || !strcasecmp(ce, "x-gzip") || !strcasecmp(ct, "application/x-gzip")) {
			      DPS_THREADINFO(Indexer, "UnGzip", s);
			      DpsUnGzip(Indexer, mDoc);
			      DpsVarListReplaceInt(&mDoc->Sections, "Content-Length", mDoc->Buf.buf - mDoc->Buf.content + (int)mDoc->Buf.size);
			    } else if(!strcasecmp(ce, "deflate") || !strcasecmp(ct, "application/deflate")) {
			      DPS_THREADINFO(Indexer, "Inflate", s);
			      DpsInflate(Indexer, mDoc);
			      DpsVarListReplaceInt(&mDoc->Sections, "Content-Length", mDoc->Buf.buf - mDoc->Buf.content + (int)mDoc->Buf.size);
			    }else if(!strcasecmp(ce, "compress") || !strcasecmp(ce, "x-compress") || !strcasecmp(ct, "application/x-compress")) {
			      DPS_THREADINFO(Indexer, "Uncompress", s);
			      DpsUncompress(Indexer, mDoc);
			      DpsVarListReplaceInt(&mDoc->Sections, "Content-Length", mDoc->Buf.buf - mDoc->Buf.content + (int)mDoc->Buf.size);
			    }else
#endif
			    if(!strcasecmp(ce, "identity") || !strcasecmp(ce, "")) {
				/* Nothing to do*/
			    }else{
			      DpsLog(Indexer,DPS_LOG_ERROR,"Unsupported Content-Encoding");
/*	          DpsVarListReplaceInt(&mDoc->Sections, "Status", status = DPS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);*/
			    }
			    result = DpsSitemapParse(Indexer, mDoc->Buf.content);
			    DPS_FREE(ct);
			  }
			}
		  }else
		  if((!(strncasecmp(s, "Crawl-delay", 11))) && (rule)) {
		    e = s + 12; DPS_SKIP(e, " \t"); s = e;
		    DPS_SKIPN(e, " \t"); *e = '\0';
			  if(AddRobotRule(Indexer, robot, DPS_METHOD_CRAWLDELAY, s, 1)) {
				  DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
					return(DPS_ERROR);
			  }
		  }
		}
		s = lt;
		while(*lt && (*lt != NL_CHAR) && (*lt != CR_CHAR)) lt++;
		if (*lt == CR_CHAR) *lt++ = '\0';
		if (*lt) *lt++ = '\0';

	}
	if (robot->nrules == 0) {
	  DpsLog(Indexer, DPS_LOG_DEBUG, "RobotsParse: no valid rules specified, allow all by default");
	  if(AddRobotRule(Indexer, robot, DPS_METHOD_GET, "/", 1)) {
	    DpsLog(Indexer, DPS_LOG_ERROR, "AddRobotRule error: no memory ?");
	    return(DPS_ERROR);
	  }
	}
	return(DPS_OK);
}
