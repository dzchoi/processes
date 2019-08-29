#!/usr/bin/env perl
use strict;
use File::Spec::Functions qw(rel2abs);
use File::Basename;

our $INCLUDE = dirname(rel2abs $0);
our $A_OUT = "a.out";

chdir "/tmp";

sub build_a_out($) {
    system "echo '$_[0]' |clang++ -I$INCLUDE -lpthread -xc++ -"
        and die "clang++ error: $?";
}

sub start_a_out($) {
    my $parent_pid = $$;

    defined(my $child_pid = fork) or die "fork error: $!";
    unless ( $child_pid ) {
        exec "$_[0]";
        kill 2, $parent_pid;  # SIGINT
        die "$A_OUT error: $!";
    }

    return $child_pid;
}

build_a_out <<EOF;
#include "process.hpp"
#include "fdstream.hpp"
#include <iostream>

int main()
{
    process proc { "sleep", "3" };
    proc.wait();  // not to make proc an orphan process
}
EOF

my $pid = start_a_out "./a.out";
print "pid = $pid";

my $ps = `ps`;
print $ps;

waitpid $pid, 0;

