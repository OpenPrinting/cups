#!/bin/sh
#
# Test the lpadmin command.
#
# Copyright © 2020-2024 by OpenPrinting.
# Copyright © 2007-2018 by Apple Inc.
# Copyright © 1997-2005 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

echo "Add Printer Test"
echo ""
echo "    lpadmin -p Test3 -v file:/dev/null -E -m drv:///sample.drv/deskjet.ppd"
$runcups $VALGRIND ../systemv/lpadmin -p Test3 -v file:/dev/null -E -m drv:///sample.drv/deskjet.ppd 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	if test -f $CUPS_SERVERROOT/ppd/Test3.ppd; then
		echo "    PASSED"
	else
		echo "    FAILED (No PPD)"
		exit 1
	fi
fi
echo ""

echo "Modify Printer Test"
echo ""
echo "    lpadmin -p Test3 -v file:/tmp/Test3 -o PageSize=A4"
$runcups $VALGRIND ../systemv/lpadmin -p Test3 -v file:/tmp/Test3 -o PageSize=A4 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Delete Printer Test"
echo ""
echo "    lpadmin -x Test3"
$runcups $VALGRIND ../systemv/lpadmin -x Test3 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Add Shared Printer Test"
echo ""
echo "    lpadmin -p Test3 -E -v ipp://localhost:$IPP_PORT/printers/Test2 -m everywhere"
$runcups $VALGRIND ../systemv/lpadmin -p Test3 -E -v ipp://localhost:$IPP_PORT/printers/Test2 -m everywhere 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Add a printer for cupSNMP/IPPSupplies test"
echo ""
echo "    lpadmin -p Test4 -E -v file:/dev/null -m drv:///sample.drv/zebra.ppd"
$runcups $VALGRIND ../systemv/lpadmin -p Test4 -E -v file:/dev/null -m drv:///sample.drv/zebra.ppd 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Turn on cupsSNMP/IPPSupplies option"
echo ""
echo "    lpadmin -p Test4 -o cupsSNMPSupplies=true -o cupsIPPSupplies=true"
$runcups $VALGRIND ../systemv/lpadmin -p Test4 -o cupsSNMPSupplies=true -o cupsIPPSupplies=true 2>&1
grep '*cupsSNMPSupplies: True' $BASE/ppd/Test4.ppd
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
grep '*cupsIPPSupplies: True' $BASE/ppd/Test4.ppd
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Turn on cupsSNMP/IPPSupplies option"
echo ""
echo "    lpadmin -p Test4 -o cupsSNMPSupplies=false -o cupsIPPSupplies=false"
$runcups $VALGRIND ../systemv/lpadmin -p Test4 -o cupsSNMPSupplies=false -o cupsIPPSupplies=false 2>&1
grep '*cupsSNMPSupplies: False' $BASE/ppd/Test4.ppd
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
grep '*cupsIPPSupplies: False' $BASE/ppd/Test4.ppd
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""

echo "Delete the printer with cupsSNMP/IPPSupplies"
echo ""
echo "    lpadmin -x Test4"
$runcups $VALGRIND ../systemv/lpadmin -x Test4 2>&1
if test $? != 0; then
	echo "    FAILED"
	exit 1
else
	echo "    PASSED"
fi
echo ""
