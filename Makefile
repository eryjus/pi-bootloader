#####################################################################################################################
##
##  Makefile -- This is the driving build, interfacing with `tup` and taking care of other things
##
##          Copyright (c)  2018 -- Adam Clark
##          Licensed under the BEER-WARE License, rev42 (see LICENSE.md)
##
## ------------------------------------------------------------------------------------------------------------------
##
##     Date      Tracker  Version  Pgmr  Description
##  -----------  -------  -------  ----  ---------------------------------------------------------------------------
##  2018-Dec-25  Initial   0.0.1   ADCL  Initial version
##
#####################################################################################################################


##
## -- Keep the commands to ourselves
##    ------------------------------
.SILENT:


## 
## -- Build everything
##    ----------------
.PHONY: all
all: 
	tup

