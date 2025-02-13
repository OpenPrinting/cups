#!/usr/bin/perl
use strict;
use warnings;

# Läs in hela filen från stdin
my @lines = <STDIN>;

foreach my $line (@lines) {
    # Ta bort HTML-taggar
    $line =~ s/<[^>]+>//g;
    
    # Ta bort allt inom { }
    $line =~ s/\{.*?\}//g;
    
    # Ta bort &nbsp;
    $line =~ s/&nbsp;//g;
    
    # Ta bort tomma rader eller rader med bara whitespace
    next if $line =~ /^\s*$/;
    
    # Skriv ut den rensade raden
    print "$line\n";
}

