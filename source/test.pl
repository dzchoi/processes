#!/usr/bin/env perl
use strict;
use File::Spec::Functions qw(rel2abs);
use File::Basename;

our $INCLUDE = dirname(rel2abs $0);
our $TEST_CPP = "test.cpp";
our $A_OUT = "a.out";

chdir "/tmp";

sub build_test_cpp($) {
    open FILE, '>', $TEST_CPP or die "$TEST_CPP: $!\n";
    print FILE $_[0];
    close FILE;
    system "clang++", "-I$INCLUDE", $TEST_CPP, "-lpthread" and die $!;
}

build_test_cpp <<EOF;
#include "process.hpp"
#include "fdstream.hpp"
#include <iostream>

int main()
{
    process proc { "sleep", "3" };
    proc.wait();  // not to make proc an orphan process
}
EOF

system "$A_OUT &" and die $!;

my $ps = `ps`;
print $ps;
