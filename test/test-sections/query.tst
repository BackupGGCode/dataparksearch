FIELDS=OFF;
SELECT dict.word,dict.intag,url.crc32,last_mod_time,url.url FROM dict, url WHERE url.rec_id=dict.url_id ORDER BY url.crc32,dict.intag;
SELECT dict.word,dict.intag,url.crc32,url.url FROM crossdict dict, url WHERE url.rec_id=dict.url_id ORDER BY url.crc32,dict.intag;
SELECT status, docsize, hops, crc32, last_mod_time, url FROM url ORDER BY status, crc32;
SELECT url.status,url.crc32,url.url,urlinfo.sname,urlinfo.sval FROM url,urlinfo WHERE url.rec_id=urlinfo.url_id ORDER BY url.status,url.crc32,lower(urlinfo.sname);

SELECT url FROM url WHERE url='http://site/';
