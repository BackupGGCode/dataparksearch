#skip !0 testenv DPS_TEST_ROOT
#skip !0 testenv DPS_TEST_DIR
#skip !0 testenv DPS_TEST_DBADDR0
#skip !0 testenv DPS_SHARE_DIR
#skip !0 testenv INDEXER
skip !0 exec $(INDEXER) -Echeck  $(DPS_TEST_DIR)/indexer.conf >> $(DPS_TEST_LOG) 2>&1

fail 20 exec $(INDEXER) -Edrop   $(DPS_TEST_DIR)/indexer.conf >> $(DPS_TEST_LOG) 2>&1
fail !0 exec $(INDEXER) -Ecreate $(DPS_TEST_DIR)/indexer.conf >> $(DPS_TEST_LOG) 2>&1
fail !0 exec $(INDEXER) -Eindex  $(DPS_TEST_DIR)/indexer.conf >> $(DPS_TEST_LOG) 2>&1
fail !0 exec $(INDEXER) -Esqlmon $(DPS_TEST_DIR)/indexer.conf < $(DPS_TEST_DIR)/query.tst > $(DPS_TEST_DIR)/query.rej 2>&1


fail !0 exec $(SEARCH) "body1+NEAR+body2&m=bool" > $(DPS_TEST_DIR)/search.rej 2>&1
fail !0 exec $(SEARCH) "body2+NEAR+body1&m=bool" > $(DPS_TEST_DIR)/search2.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search.rej $(DPS_TEST_DIR)/search2.rej
fail !0 exec rm -f $(DPS_TEST_DIR)/search2.rej

fail !0 exec $(SEARCH) "body1+NEAR+body20&m=bool" > $(DPS_TEST_DIR)/search.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search.rej $(DPS_TEST_DIR)/search.res

fail !0 exec $(SEARCH) "body20+ANY+body18&m=bool" > $(DPS_TEST_DIR)/search.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search.rej $(DPS_TEST_DIR)/search.res

fail !0 exec $(SEARCH) "body18+ANY+body20&m=bool" > $(DPS_TEST_DIR)/search2.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search2.rej $(DPS_TEST_DIR)/search2.res

fail !0 exec $(SEARCH) "body1+AND+NOT+body10&m=bool"> $(DPS_TEST_DIR)/search3.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search3.rej $(DPS_TEST_DIR)/search3.res

fail !0 exec $(SEARCH) "body19+NEAR+NOT+body20&m=bool"> $(DPS_TEST_DIR)/search4.rej 2>&1
fail !0 mdiff $(DPS_TEST_DIR)/search4.rej $(DPS_TEST_DIR)/search4.res


#fail !0 exec $(SEARCH) "(body1+NEAR+body18)+OR+(body1+NEAR+body15)&m=bool&LogLevel=5" > $(DPS_TEST_DIR)/search.rej 2>&1
#fail !0 exec $(SEARCH) "body1+NEAR+(body18+OR+body15)&m=bool&LogLevel=5" > $(DPS_TEST_DIR)/search2.rej 2>&1
#fail !0 mdiff $(DPS_TEST_DIR)/search.rej $(DPS_TEST_DIR)/search2.rej


fail !0 mdiff $(DPS_TEST_DIR)/query.rej $(DPS_TEST_DIR)/query.res

fail !0 exec rm -f $(DPS_TEST_DIR)/query.rej
fail !0 exec rm -f $(DPS_TEST_DIR)/search.rej
fail !0 exec rm -f $(DPS_TEST_DIR)/search2.rej
fail !0 exec rm -f $(DPS_TEST_DIR)/search3.rej
fail !0 exec rm -f $(DPS_TEST_DIR)/search4.rej

pass 0 exec  $(INDEXER) -Edrop $(DPS_TEST_DIR)/indexer.conf >> $(DPS_TEST_LOG) 2>&1
