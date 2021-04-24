#!/usr/bin/expect
set password [lindex $argv 0]
spawn scp server mmtian@node20:/home/mmtian
set timeout 300
expect "*password:"
send "$password\n"
expect eof

set password [lindex $argv 0]
spawn scp server mmtian@node22:/home/mmtian
set timeout 300
expect "*assword:"
send "$password\n"
expect eof


