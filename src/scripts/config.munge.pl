#! /usr/bin/env perl

die "exactly one argument (variable name) required" if $#ARGV != 0;

my %substs;
while (<STDIN>) {
    while (/@(.+?)@/) {
        print STDERR "$.:processing $1\n";
        $substs{$1} = $.;
        $_ = $';
    }
}

print "$ARGV[0] = sed";
foreach my $s (keys %substs) {
    print " \\\n  -e 's![@]$s\[@]!\$($s)!g'";
}
print "\n";
