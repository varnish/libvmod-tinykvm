#!/bin/bash

varnishtest -v -Dvmod_example="example from \"/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_example.so\"" tests/test01.vtc
