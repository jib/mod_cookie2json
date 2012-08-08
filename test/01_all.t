#!/usr/bin/perl

### XXX run out of semaphores? Can happen in testing:
### ipcs  | grep 0x0 | awk '{print $2}' | xargs -I% ipcrm -s %

use strict;
use warnings;
use Test::More      'no_plan';
use HTTP::Date      qw[str2time];
use Getopt::Long;
use Data::Dumper;
use HTTP::Cookies;
use LWP::UserAgent;

my $Base                = "http://localhost:7000";
my $Debug               = 0;

GetOptions(
    'base=s'            => \$Base,
    'debug'             => \$Debug,
);

my %Map     = (
    ### module is not turned on
    none    => {
        no_cookie   => 1,
    },

    ### straight forward conversion
    basic   => { },

);

for my $endpoint ( sort keys %Map ) {

    my $cfg = $Map{ $endpoint };

    ### Defaults in case not provided
    my $header      = $cfg->{header}        || [ ];

    ### build the test
    my $url     = "$Base/$endpoint";
    my $ua      = LWP::UserAgent->new();
    my @req     = ($url, @$header );

    diag "Sending: @req" if $Debug;

    ### make the request
    my $res     = $ua->get( @req );
    diag $res->as_string if $Debug;

    ### inspect
    ok( $res,                   "Got /$endpoint" );
    is( $res->code, "204",      "   HTTP Response = 204" );

}
