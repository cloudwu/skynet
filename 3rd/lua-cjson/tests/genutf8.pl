#!/usr/bin/env perl

# Create test comparison data using a different UTF-8 implementation.

# The generated utf8.dat file must have the following MD5 sum:
#       cff03b039d850f370a7362f3313e5268

use strict;

# 0xD800 - 0xDFFF are used to encode supplementary codepoints
# 0x10000 - 0x10FFFF are supplementary codepoints
my (@codepoints) = (0 .. 0xD7FF, 0xE000 .. 0x10FFFF);

my $utf8 = pack("U*", @codepoints);
defined($utf8) or die "Unable create UTF-8 string\n";

open(FH, ">:utf8", "utf8.dat")
    or die "Unable to open utf8.dat: $!\n";
print FH $utf8
    or die "Unable to write utf8.dat\n";
close(FH);

# vi:ai et sw=4 ts=4:
