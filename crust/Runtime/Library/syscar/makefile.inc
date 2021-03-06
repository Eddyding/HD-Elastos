#==========================================================================
# Copyright (c) 2000-2008,  Elastos, Inc.  All Rights Reserved.
#==========================================================================

$(SPECIAL_TARGET):
##########################################################
# copy export head files generated by *.car to system\inc
##########################################################
#created by .car files
	@echo Copying ElastosCore.* to system...
	$(V) $(CP) ElastosCore.cls \
		$(XDK_TARGETS) $(BLACKHOLE)
	$(V) $(CP) _ElastosCore.h \
		$(XDK_USER_INC) $(BLACKHOLE)
ifneq "$(SystemRoot)" ""
	$(V) $(CP) ElastosCore.h \
		$(XDK_USER_INC)\ElastosCore.h $(BLACKHOLE)
else
	$(V) $(CP) ElastosCore.h \
		$(XDK_USER_INC)/ElastosCore.h $(BLACKHOLE)
endif
	@touch $@

