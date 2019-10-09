#!/usr/bin/env python3
import sys
input_vcl = sys.argv[1]
output_file = sys.argv[2]

f = open(input_vcl, "r")
builtin_vcl = ""
for line in f:
	# add "" around lines and end with \
	builtin_vcl += "\t\"" + line.rstrip().replace("\"", "\\\"") + "\\n\"\n"
f.close()

f = open(output_file, "w")
f.write(
	"""/*
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit builtin.vcl instead and run make
 *
 */
#include "config.h"
#include "mgt/mgt.h"

const char * const builtin_vcl =
""")
f.write(builtin_vcl.rstrip() + ";\n")
f.close()
