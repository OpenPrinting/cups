#!/bin/sh
#
# Script to wait for jobs to complete.
#
# Copyright © 2020-2024 by OpenPrinting.
# Copyright © 2008-2019 by Apple Inc.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

#
# Get timeout from command-line
#

if test $# = 1; then
	timeout=$1
else
	timeout=360
fi

#
# Figure out the proper echo options...
#

if (echo "testing\c"; echo 1,2,3) | grep c >/dev/null; then
        ac_n=-n
        ac_c=
else
        ac_n=
        ac_c='\c'
fi

#
# Check whether we have any jobs to wait for...
#

jobs=`$runcups ../systemv/lpstat 2>/dev/null | wc -l | tr -d ' '`
if test $jobs = 0; then
	exit 0
fi

#
# We do, let the tester know what is going on...
#

echo $ac_n "Waiting for jobs to complete...$ac_c"
oldjobs=0

while test $timeout -gt 0; do
	jobs=`$runcups ../systemv/lpstat 2>/dev/null | wc -l | tr -d ' '`
	if test $jobs = 0; then
		break
	fi

	if test $jobs != $oldjobs; then
		echo $ac_n "$jobs...$ac_c"
		oldjobs=$jobs
	fi

	sleep 5
	timeout=`expr $timeout - 5`
done

echo ""
